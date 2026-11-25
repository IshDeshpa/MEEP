#include "state.hpp"
#include "config.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>

__device__ static CubeTile stateTransition(DeviceLUTView lut, CubeTile tile, size_t face_phase);
__global__ static void updateTile(DeviceWorldView buffers, DeviceLUTView lut,
                           size_t tiles_y, size_t tiles_z, size_t tile_count,
                           size_t tile_phase, size_t face_phase);

static_assert(FACE_TRANSITION && !CUBE_TRANSITION,
              "Only face transitions are implemented");

// WorldState: CPU ownership, initialization, and tick submission.

std::tuple<size_t, size_t, size_t> WorldState::validateWorldFile(const char* f) {
    std::filesystem::path path(f);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("World file does not exist: " + path.string());
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open world file: " + path.string());
    }

    // Read the dimensions from the file
    size_t n, m, k;
    if (!file.read(reinterpret_cast<char*>(&n), sizeof(n)) ||
        !file.read(reinterpret_cast<char*>(&m), sizeof(m)) ||
        !file.read(reinterpret_cast<char*>(&k), sizeof(k))) {
        throw std::runtime_error("Failed to read world dimensions");
    }

    return std::make_tuple(n, m, k);
}

WorldState::WorldState(std::tuple<size_t, size_t, size_t> dims)
    : n(std::get<0>(dims)), m(std::get<1>(dims)), k(std::get<2>(dims)),
      n_pad(0), m_pad(0), k_pad(0), world(nullptr), d_curr(nullptr),
      d_next(nullptr), device_view{}, world_tick_stream(nullptr) {
    const auto max = std::numeric_limits<size_t>::max();
    if (n == 0 || m == 0 || k == 0) {
        throw std::invalid_argument("World dimensions must be positive");
    }
    const auto max_dimension =
        static_cast<size_t>(std::numeric_limits<int>::max()) - 2;
    if (n > max_dimension || m > max_dimension || k > max_dimension) {
        throw std::overflow_error("Padded world dimensions exceed ivec3 range");
    }
    if (n > max - 2 || m > max - 2 || k > max - 2) {
        throw std::overflow_error("Padded world dimensions overflow");
    }
    n_pad = n + 2;
    m_pad = m + 2;
    k_pad = k + 2;
    const size_t max_coordinate =
        static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (n_pad > max_coordinate || m_pad > max_coordinate ||
        k_pad > max_coordinate) {
        throw std::overflow_error("Padded dimensions exceed signed coordinate range");
    }
    if (n_pad > max / m_pad ||
        n_pad * m_pad > max / k_pad ||
        n_pad * m_pad * k_pad > max / sizeof(Cell)) {
        throw std::overflow_error("Padded world allocation size overflows");
    }
    const size_t cell_count = n_pad * m_pad * k_pad;
    const size_t bytes = cell_count * sizeof(Cell);

    size_t free_bytes, total_bytes;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    if (bytes > free_bytes / 2) {
        throw std::runtime_error(
            "Insufficient free GPU memory for both padded world buffers");
    }

    std::unique_ptr<Cell[]> initial(new Cell[cell_count]);
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
        CUDA_CHECK(cudaStreamCreate(&world_tick_stream));
        CUDA_CHECK(cudaMalloc(&d_curr, bytes));
        CUDA_CHECK(cudaMalloc(&d_next, bytes));
        CUDA_CHECK(cudaMemcpy(d_curr, initial.get(), bytes, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(d_next, initial.get(), bytes, cudaMemcpyHostToDevice));
    } catch (...) {
        // Construction failure does not run the destructor.
        if (d_next) cudaFree(d_next);
        if (d_curr) cudaFree(d_curr);
        if (world_tick_stream) cudaStreamDestroy(world_tick_stream);
        d_next = nullptr;
        d_curr = nullptr;
        world_tick_stream = nullptr;
        throw;
    }
    world = initial.release();
    device_view = {d_curr, d_next, n_pad, m_pad, k_pad};
}

WorldState::WorldState(size_t n, size_t m, size_t k) : WorldState(std::make_tuple(n, m, k)) {}

WorldState::WorldState(const char* f) : WorldState(validateWorldFile(f)) {
    std::filesystem::path path(f);
    const auto file_bytes = std::filesystem::file_size(path);
    const auto expected_size = n * m * k * sizeof(Cell);

    if ((file_bytes - 3*sizeof(size_t)) != expected_size) {
        throw std::runtime_error("File size does not match expected world size");
    }

    std::ifstream file(path, std::ios::binary);
    std::vector<Cell> h_world(n * m * k);
    file.seekg(3 * sizeof(size_t), std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(h_world.data()), expected_size)) {
        throw std::runtime_error("Failed to read world cells from file");
    }

    for (size_t x = 0; x < n_pad; ++x) {
        for (size_t y = 0; y < m_pad; ++y) {
            for (size_t z = 0; z < k_pad; ++z) {
                if (!(x == 0 || x == n_pad - 1 ||
                      y == 0 || y == m_pad - 1 ||
                      z == 0 || z == k_pad - 1)) {
                    world[paddedIndex(x, y, z)] = h_world[unPaddedIndex(x - 1, y - 1, z - 1)];
                }
            }
        }
    }
    CUDA_CHECK(cudaMemcpy(d_curr, world, n_pad * m_pad * k_pad * sizeof(Cell), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_next, world, n_pad * m_pad * k_pad * sizeof(Cell), cudaMemcpyHostToDevice));
}

WorldState::~WorldState() {
    delete[] world;
    cudaFree(d_curr);
    cudaFree(d_next);
    cudaStreamDestroy(world_tick_stream);
}

size_t WorldState::index(size_t x, size_t y, size_t z) const {
    return device_view.index(static_cast<std::ptrdiff_t>(x),
                              static_cast<std::ptrdiff_t>(y),
                              static_cast<std::ptrdiff_t>(z));
}

size_t WorldState::index(worldcoord c) const {
    if (c.x < -1 || c.y < -1 || c.z < -1 ||
        static_cast<std::ptrdiff_t>(c.x) > static_cast<std::ptrdiff_t>(n) ||
        static_cast<std::ptrdiff_t>(c.y) > static_cast<std::ptrdiff_t>(m) ||
        static_cast<std::ptrdiff_t>(c.z) > static_cast<std::ptrdiff_t>(k)) {
        throw std::out_of_range("Cell coordinate is outside the padded world");
    }
    return device_view.index(c.x, c.y, c.z);
}

Cell& WorldState::operator()(worldcoord c) {
    return world[index(c)];
}

const Cell& WorldState::operator()(worldcoord c) const {
    return world[index(c)];
}

size_t WorldState::unPaddedIndex(size_t x, size_t y, size_t z) const {
    return z + k * (y + m * x);
}

size_t WorldState::paddedIndex(size_t x, size_t y, size_t z) const {
    return z + k_pad * (y + m_pad * x);
}

void WorldState::swapBuffers() {
    std::swap(d_curr, d_next); // swap in-CPU copies
    device_view = {d_curr, d_next, n_pad, m_pad, k_pad};
}

DeviceWorldView WorldState::deviceView() const {
    return device_view;
}

// DeviceWorldView: padded indexing and tile access.
__host__ __device__
std::size_t DeviceWorldView::index(std::size_t x, std::size_t y,
                                  std::size_t z) const noexcept {
    return static_cast<std::size_t>(z + 1) +
           k_pad * (static_cast<std::size_t>(y + 1) +
                    m_pad * static_cast<std::size_t>(x + 1));
}

__host__ __device__
Cell DeviceWorldView::readCell(std::size_t x, std::size_t y,
                               std::size_t z) const noexcept {
#ifdef __CUDA_ARCH__
    return curr[index(x, y, z)];
#else
    Cell c;
    cudaMemcpy(&c, &curr[index(x, y, z)], sizeof(Cell), cudaMemcpyDeviceToHost);
    return c;
#endif
}

__host__ __device__
Cell DeviceWorldView::readCell(worldcoord c) const noexcept {
    return readCell(static_cast<std::size_t>(c.x),
                    static_cast<std::size_t>(c.y),
                    static_cast<std::size_t>(c.z));
}

__host__ __device__
CubeTile DeviceWorldView::readTile(std::size_t x, std::size_t y,
                                   std::size_t z) const noexcept {
    CubeTile tile;
    const auto base = index(x, y, z);
    for (unsigned c = 0; c < 8; ++c) {
        const std::size_t dx = (c >> 2) & 1;
        const std::size_t dy = (c >> 1) & 1;
        const std::size_t dz = c & 1;

        const std::size_t ind = base + m_pad * k_pad * dx + k_pad * dy + dz;
#ifdef __CUDA_ARCH__
        tile.cells[c] = curr[ind];
#else
        cudaMemcpy(&tile.cells[c], &curr[ind], sizeof(Cell), cudaMemcpyDeviceToHost);
#endif
    }
    return tile;
}

__host__ __device__
CubeTile DeviceWorldView::readTile(worldcoord c) const noexcept {
    return readTile(static_cast<std::size_t>(c.x),
                   static_cast<std::size_t>(c.y),
                   static_cast<std::size_t>(c.z));
}

__host__ __device__
void DeviceWorldView::writeCell(std::size_t x, std::size_t y,
                                std::size_t z, const Cell cell) const noexcept {
#ifdef __CUDA_ARCH__
    next[index(x, y, z)] = cell;
#else
    Cell c;
    cudaMemcpy(&next[index(x, y, z)], &cell, sizeof(Cell), cudaMemcpyHostToDevice);
#endif
}

__host__ __device__
void DeviceWorldView::writeCell(worldcoord c, Cell cell) const noexcept {
    writeCell(static_cast<std::size_t>(c.x),
                static_cast<std::size_t>(c.y),
                static_cast<std::size_t>(c.z), cell);
}

__host__ __device__
void DeviceWorldView::writeTile(std::size_t x, std::size_t y,
                                std::size_t z,
                                const CubeTile& tile) const noexcept {
    const auto base = index(x, y, z);
    for (unsigned c = 0; c < 8; ++c) {
        const std::size_t dx = (c >> 2) & 1;
        const std::size_t dy = (c >> 1) & 1;
        const std::size_t dz = c & 1;
        const std::size_t ind = base + m_pad * k_pad * dx + k_pad * dy + dz;
#ifdef __CUDA_ARCH__
        next[ind] = tile.cells[c];
#else
        cudaMemcpy(&next[ind], &tile.cells[c], sizeof(Cell), cudaMemcpyHostToDevice);
#endif
    }
}

__host__ __device__
void DeviceWorldView::writeTile(worldcoord c, const CubeTile& tile) const noexcept {
    writeTile(static_cast<std::size_t>(c.x),
            static_cast<std::size_t>(c.y),
            static_cast<std::size_t>(c.z), tile);
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
    updateTile<<<blocks, THREADS_PER_BLOCK, 0, world_tick_stream>>>(
        device_view, lut.deviceView(), tiles_y, tiles_z, tile_count,
        tile_phase, face_phase);
    CUDA_CHECK(cudaGetLastError()); // Checks submission, not GPU completion.
}

void WorldState::waitForTick() {
    // Complete this world's update before buffer swaps or renderer access.
    CUDA_CHECK(cudaStreamSynchronize(world_tick_stream));
}

// GPU update kernel and state transition.

__global__ static void updateTile(DeviceWorldView buffers, DeviceLUTView lut,
                           size_t tiles_y, size_t tiles_z, size_t tile_count,
                           size_t tile_phase, size_t face_phase) {
    const size_t tile_id = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (tile_id >= tile_count)
        return;

    // Coordinates in the tile grid: z changes fastest, then y, then x.
    const size_t tz = tile_id % tiles_z;
    const size_t ty = (tile_id / tiles_z) % tiles_y;
    const size_t tx = tile_id / (tiles_z * tiles_y);

    // Partition origins are padded coordinates; expose visible-world origins.
    const auto x = static_cast<std::ptrdiff_t>(2 * tx + tile_phase) - 1;
    const auto y = static_cast<std::ptrdiff_t>(2 * ty + tile_phase) - 1;
    const auto z = static_cast<std::ptrdiff_t>(2 * tz + tile_phase) - 1;

    const CubeTile tile = buffers.readTile(x, y, z);
    const CubeTile next_tile = stateTransition(lut, tile, face_phase);
    buffers.writeTile(x, y, z, next_tile);

    // Uncovered cells are padding only and retain their initialized BLOCK value.
    // TODO: swap buffers after completion. LUT calls are still placeholders.
}


__device__ static CubeTile stateTransition(DeviceLUTView lut, CubeTile tile, size_t face_phase) {
    CubeTile next_tile = tile; // Keep reserved state bits initialized.
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
    return next_tile;
}
