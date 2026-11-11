#include "state.hpp"
#include "config.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

static_assert(THREADS_PER_BLOCK > 0 && THREADS_PER_BLOCK % 32 == 0);

namespace {
glm::ivec3 checkedDimensions(size_t n, size_t m, size_t k) {
    if (n == 0 || m == 0 || k == 0) {
        throw std::invalid_argument("World dimensions must be positive");
    }
    const auto max_dim =
        static_cast<size_t>(std::numeric_limits<int>::max()) - 2;
    if (n > max_dim || m > max_dim || k > max_dim) {
        throw std::overflow_error("Padded world dimensions exceed ivec3 range");
    }
    return glm::ivec3(static_cast<int>(n), static_cast<int>(m), static_cast<int>(k));
}

__device__ CubeTile stateTransition(DeviceLUTView lut, CubeTile tile, size_t face_phase);
__global__ void updateTile(DeviceWorldView buffers, DeviceLUTView lut,
                           size_t tiles_y, size_t tiles_z, size_t tile_count,
                           size_t tile_phase, size_t face_phase);
} // namespace

// WorldState: CPU ownership, initialization, and tick submission.

WorldState::WorldState(size_t n, size_t m, size_t k)
    : dimensions(checkedDimensions(n, m, k)), n_pad(0), m_pad(0), k_pad(0),
      world(nullptr), d_curr(nullptr), d_next(nullptr),
      d_phase0_dirty_curr(nullptr), d_phase0_dirty_next(nullptr),
      d_phase1_dirty_curr(nullptr), d_phase1_dirty_next(nullptr) {
    const auto max = std::numeric_limits<size_t>::max();
    n_pad = n + 2;
    m_pad = m + 2;
    k_pad = k + 2;
    if (n_pad > max / m_pad ||
        n_pad * m_pad > max / k_pad ||
        n_pad * m_pad * k_pad > max / sizeof(Cell)) {
        throw std::overflow_error("Padded world allocation size overflows");
    }
    const size_t cell_count = n_pad * m_pad * k_pad;
    const size_t bytes = cell_count * sizeof(Cell);

    size_t phase0_bytes = 0, phase1_bytes = 0;
#if DIRTY_STATE_EXCLUDE_ONLY
    const size_t phase0_tiles = (n_pad / 2) * (m_pad / 2) * (k_pad / 2);
    const size_t phase1_tiles = ((n_pad - 1) / 2) * ((m_pad - 1) / 2) * ((k_pad - 1) / 2);
    phase0_bytes = ((phase0_tiles + 31) / 32) * sizeof(uint32_t);
    phase1_bytes = ((phase1_tiles + 31) / 32) * sizeof(uint32_t);
#endif
    // Two world buffers plus two bitmaps per partition; check arithmetic first.
    if (bytes > max / 2 || phase0_bytes > (max - 2 * bytes) / 2 ||
        phase1_bytes > (max - 2 * bytes - 2 * phase0_bytes) / 2) {
        throw std::overflow_error("World and dirty bitmap allocation size overflows");
    }
    const size_t required_bytes = 2 * bytes + 2 * phase0_bytes + 2 * phase1_bytes;
    size_t free_bytes, total_bytes;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    if (required_bytes > free_bytes) {
        throw std::runtime_error(
            "Insufficient free GPU memory for world buffers and dirty bitmaps");
    }

    auto initial = std::make_unique<Cell[]>(cell_count);
    for (size_t x = 0; x < n_pad; ++x) {
        for (size_t y = 0; y < m_pad; ++y) {
            for (size_t z = 0; z < k_pad; ++z) {
                const bool border =
                    x == 0 || x == n_pad - 1 ||
                    y == 0 || y == m_pad - 1 ||
                    z == 0 || z == k_pad - 1;
                initial[paddedIndex(x, y, z)] =
                    Cell{border ? Element::BLOCK : Element::AIR, 0};
            }
        }
    }

    try {
        CUDA_CHECK(cudaMalloc(&d_curr, bytes));
        CUDA_CHECK(cudaMalloc(&d_next, bytes));
#if DIRTY_STATE_EXCLUDE_ONLY
        CUDA_CHECK(cudaMalloc(&d_phase0_dirty_curr, phase0_bytes));
        CUDA_CHECK(cudaMalloc(&d_phase0_dirty_next, phase0_bytes));
        CUDA_CHECK(cudaMalloc(&d_phase1_dirty_curr, phase1_bytes));
        CUDA_CHECK(cudaMalloc(&d_phase1_dirty_next, phase1_bytes));
        CUDA_CHECK(cudaMemset(d_phase0_dirty_curr, 0, phase0_bytes));
        CUDA_CHECK(cudaMemset(d_phase1_dirty_curr, 0, phase1_bytes));
        // Both partitions must run on their first visit, even if the other is static.
        CUDA_CHECK(cudaMemset(d_phase0_dirty_next, 0xff, phase0_bytes));
        CUDA_CHECK(cudaMemset(d_phase1_dirty_next, 0xff, phase1_bytes));
#endif
        CUDA_CHECK(cudaMemcpy(d_curr, initial.get(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_next, initial.get(), bytes, cudaMemcpyHostToDevice));
    } catch (...) {
        // Construction failure does not run the destructor.
        if (d_phase0_dirty_curr) cudaFree(d_phase0_dirty_curr);
        if (d_phase0_dirty_next) cudaFree(d_phase0_dirty_next);
        if (d_phase1_dirty_curr) cudaFree(d_phase1_dirty_curr);
        if (d_phase1_dirty_next) cudaFree(d_phase1_dirty_next);
        if (d_next) cudaFree(d_next);
        if (d_curr) cudaFree(d_curr);
        d_next = nullptr;
        d_curr = nullptr;
        throw;
    }
    world = initial.release();
}

WorldState::~WorldState() {
    delete[] world;
    cudaFree(d_curr);
    cudaFree(d_next);
    cudaFree(d_phase0_dirty_curr);
    cudaFree(d_phase0_dirty_next);
    cudaFree(d_phase1_dirty_curr);
    cudaFree(d_phase1_dirty_next);
}

size_t WorldState::index(size_t x, size_t y, size_t z) const {
    return deviceView().index(static_cast<int>(x),
                               static_cast<int>(y),
                               static_cast<int>(z));
}

size_t WorldState::index(coord c) const {
    if (c.x < -1 || c.y < -1 || c.z < -1 ||
        c.x > dimensions.x ||
        c.y > dimensions.y ||
        c.z > dimensions.z) {
        throw std::out_of_range("Cell coordinate is outside the padded world");
    }
    return deviceView().index(c.x, c.y, c.z);
}

Cell& WorldState::operator()(coord c) {
    return world[index(c)];
}

const Cell& WorldState::operator()(coord c) const {
    return world[index(c)];
}

size_t WorldState::paddedIndex(size_t x, size_t y, size_t z) const {
    return z + k_pad * (y + m_pad * x);
}

DeviceWorldView WorldState::deviceView() const {
    return {d_curr, d_next, n_pad, m_pad, k_pad,
            d_phase0_dirty_curr, d_phase0_dirty_next,
            d_phase1_dirty_curr, d_phase1_dirty_next};
}

void WorldState::updateWorld(std::size_t tick, LUT& lut) {
    const size_t tile_phase = (tick >> 1) & 1; // Partition origin offset: 0 or 1.
    const size_t tiles_x = (n_pad - tile_phase) / 2;
    const size_t tiles_y = (m_pad - tile_phase) / 2;
    const size_t tiles_z = (k_pad - tile_phase) / 2;
    const size_t tile_count = tiles_x * tiles_y * tiles_z;
    const size_t face_phase = tick & 1; // 0: x faces, 1: z faces.

    if (tile_count == 0)
        return;

    const size_t blocks = 1 + (tile_count - 1) / THREADS_PER_BLOCK;
    updateTile<<<blocks, THREADS_PER_BLOCK>>>(
        deviceView(), lut.deviceView(), tiles_y, tiles_z, tile_count,
        tile_phase, face_phase);
    CUDA_CHECK(cudaGetLastError()); // Checks submission, not GPU completion.
}

void WorldState::waitForTick() {
    // Launches currently use the default stream. Move both launch and wait
    // to a dedicated simulation stream before overlapping GPU rendering.
    CUDA_CHECK(cudaStreamSynchronize(nullptr));
}

void WorldState::swapBuffers() {
    std::swap(d_curr, d_next);
}

// CubeTile Helper
__host__ __device__
bool CubeTile::operator==(const CubeTile& other) const noexcept {
    // memcpy avoids aliasing casts and is optimized into register operations.
    uint64_t tile_bits, other_bits;
    memcpy(&tile_bits, this, sizeof(tile_bits));
    memcpy(&other_bits, &other, sizeof(other_bits));
    // Derive the material mask from Cell's bitfield layout, not a byte-order guess.
    const Cell element_bits{static_cast<Element>(0xf), 0};
    uint8_t element_mask;
    memcpy(&element_mask, &element_bits, sizeof(element_mask));
    const uint64_t mask = uint64_t(element_mask) * UINT64_C(0x0101010101010101);
    return ((tile_bits ^ other_bits) & mask) == 0;
}

__host__ __device__
bool CubeTile::operator!=(const CubeTile& other) const noexcept {
    return !(*this == other);
}
// DeviceWorldView: padded indexing and tile access.
// These definitions share a translation unit with updateTile so optimization
// can inline the helpers without putting their bodies in the header.

__host__ __device__
std::size_t DeviceWorldView::index(int x, int y, int z) const noexcept {
    return static_cast<std::size_t>(z + 1) +
           k_pad * (static_cast<std::size_t>(y + 1) +
                       m_pad * static_cast<std::size_t>(x + 1));
}

__host__ __device__
CubeTile DeviceWorldView::readTile(int x, int y, int z) const noexcept {
    CubeTile tile;
    const auto base = index(x, y, z);
    for (unsigned c = 0; c < 8; ++c) {
        const std::size_t dx = (c >> 2) & 1;
        const std::size_t dy = (c >> 1) & 1;
        const std::size_t dz = c & 1;
        tile.cells[c] = curr[base + m_pad * k_pad * dx + k_pad * dy + dz];
    }
    return tile;
}

__host__ __device__
void DeviceWorldView::writeTile(int x, int y, int z,
                                const CubeTile& tile) const noexcept {
    const auto base = index(x, y, z);
    for (unsigned c = 0; c < 8; ++c) {
        const std::size_t dx = (c >> 2) & 1;
        const std::size_t dy = (c >> 1) & 1;
        const std::size_t dz = c & 1;
        next[base + m_pad * k_pad * dx + k_pad * dy + dz] = tile.cells[c];
    }
}

__host__ __device__
bool DeviceWorldView::checkTileDirty(size_t tile_phase, size_t tx, size_t ty,
                                    size_t tz) const noexcept {
    const size_t tiles_y = (m_pad - tile_phase) / 2;
    const size_t tiles_z = (k_pad - tile_phase) / 2;
    const size_t tile_id = (tx * tiles_y + ty) * tiles_z + tz;
    const uint32_t* dirty_next = tile_phase == 0 ? phase0_dirty_next : phase1_dirty_next;
    if (dirty_next[tile_id >> 5] & (1u << (tile_id & 31)))
        return true;

    const size_t prev_phase = tile_phase ^ 1;
    const size_t prev_tiles_x = (n_pad - prev_phase) / 2;
    const size_t prev_tiles_y = (m_pad - prev_phase) / 2;
    const size_t prev_tiles_z = (k_pad - prev_phase) / 2;
    const uint32_t* prev_phase_dirty = tile_phase == 0 ? phase1_dirty_next : phase0_dirty_next;
    // Phase 0 gathers offsets {-1,0}; phase 1 gathers {0,1} on each axis.
    const int offset = tile_phase == 0 ? -1 : 0;
    for (int dx = 0; dx < 2; ++dx) {
        const int px = static_cast<int>(tx) + offset + dx;
        if (px < 0 || size_t(px) >= prev_tiles_x) continue;
        for (int dy = 0; dy < 2; ++dy) {
            const int py = static_cast<int>(ty) + offset + dy;
            if (py < 0 || size_t(py) >= prev_tiles_y) continue;
            for (int dz = 0; dz < 2; ++dz) {
                const int pz = static_cast<int>(tz) + offset + dz;
                if (pz < 0 || size_t(pz) >= prev_tiles_z) continue;
                const size_t prev_id = (size_t(px) * prev_tiles_y + size_t(py)) * prev_tiles_z + size_t(pz);
                if (prev_phase_dirty[prev_id >> 5] & (1u << (prev_id & 31)))
                    return true;
            }
        }
    }
    return false;
}

// GPU update kernel and state transition.

namespace {

__global__ void updateTile(DeviceWorldView buffers, DeviceLUTView lut,
                           size_t tiles_y, size_t tiles_z, size_t tile_count,
                           size_t tile_phase, size_t face_phase) {
    const size_t tile_id = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (tile_id >= tile_count)
        return;

    // Coordinates in the tile grid: z changes fastest, then y, then x.
    const size_t tz = tile_id % tiles_z;
    const size_t ty = (tile_id / tiles_z) % tiles_y;
    const size_t tx = tile_id / (tiles_z * tiles_y);

    // Partition origins are padded coordinates; expose visible-world origins
    const auto x = static_cast<int>(2 * tx + tile_phase) - 1;
    const auto y = static_cast<int>(2 * ty + tile_phase) - 1;
    const auto z = static_cast<int>(2 * tz + tile_phase) - 1;

#if DIRTY_STATE_EXCLUDE_ONLY
    uint32_t* dirty_curr = tile_phase == 0 ? buffers.phase0_dirty_curr : buffers.phase1_dirty_curr;
    uint32_t* dirty_next = tile_phase == 0 ? buffers.phase0_dirty_next : buffers.phase1_dirty_next;
    const size_t word_idx = tile_id >> 5;
    const unsigned bit_idx = tile_id & 31;
    bool active;
    if (face_phase == 0) {
        active = buffers.checkTileDirty(tile_phase, tx, ty, tz);
        // Freeze activity before x overwrites this partition's previous changes.
        const unsigned active_ballot = __ballot_sync(0xffffffffu, active);
        if ((threadIdx.x & 31u) == 0)
            dirty_curr[word_idx] = active_ballot;
    } else {
        active = (dirty_curr[word_idx] & (1u << bit_idx)) != 0;
    }
#endif

    const CubeTile tile = buffers.readTile(x, y, z);
#if DIRTY_STATE_EXCLUDE_ONLY
    const CubeTile next_tile = active ? stateTransition(lut, tile, face_phase) : tile;
#else
    const CubeTile next_tile = stateTransition(lut, tile, face_phase);
#endif
    // Sleeping tiles still carry their complete Cell state into the next buffer.
    buffers.writeTile(x, y, z, next_tile);

#if DIRTY_STATE_EXCLUDE_ONLY
    const bool dirty = next_tile != tile;
    const unsigned ballot = __ballot_sync(0xffffffffu, dirty);
    if ((threadIdx.x & 31u) == 0) {
        if (face_phase == 0)
            dirty_next[word_idx] = ballot;
        else
            dirty_next[word_idx] |= ballot;
    }
#endif
}


__device__ CubeTile stateTransition(DeviceLUTView lut, CubeTile tile, size_t face_phase) {
    CubeTile next_tile = tile; // Keep reserved state bits initialized.
#if FACE_TRANSITION
    FaceETile face_0, face_1;

    if (face_phase == 0) { // x faces
        face_0 = packFace(tile, 2, 3, 0, 1);
        face_1 = packFace(tile, 6, 7, 4, 5);
        face_0 = lut.lookup(face_0);
        face_1 = lut.lookup(face_1);
        unpackFace(face_0, next_tile, 2, 3, 0, 1);
        unpackFace(face_1, next_tile, 6, 7, 4, 5);
    } else {              // z faces
        face_0 = packFace(tile, 3, 7, 1, 5);
        face_1 = packFace(tile, 2, 6, 0, 4);
        face_0 = lut.lookup(face_0);
        face_1 = lut.lookup(face_1);
        unpackFace(face_0, next_tile, 3, 7, 1, 5);
        unpackFace(face_1, next_tile, 2, 6, 0, 4);
    }

#elif CUBE_TRANSITION
    #error "Cube transitions are not implemented"

#else
    #error "No state transition variation selected in config.hpp"
#endif
    return next_tile;
}

} // namespace
