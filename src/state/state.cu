#include "state.hpp"
#include "config.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <limits>
#include <stdexcept>

#define TILE_PHASE_SHIFT(tile_coord, tile_phase) 2 * tile_coord + tile_phase


WorldState::WorldState(size_t n, size_t m, size_t k) //TODO: add filepath to initialization file. 
    : n(n), m(m), k(k), world(nullptr), d_curr(nullptr), d_next(nullptr) {
    const auto max = std::numeric_limits<size_t>::max();
    if (n == 0 || m == 0 || k == 0) {
        throw std::invalid_argument("World dimensions must be positive");
    }
    if (n > max / m || n * m > max / k ||
        n * m * k > max / sizeof(Cell)) {
        throw std::overflow_error("World allocation size overflows");
    }

    const auto bytes = n * m * k * sizeof(Cell);  
    CUDA_CHECK(cudaMalloc(&d_curr, bytes));
    try {
        CUDA_CHECK(cudaMalloc(&d_next, bytes));
    } catch (...) {
        // The destructor is not called if construction fails.
        cudaFree(d_curr);
        d_curr = nullptr;
        throw;
    }
}

WorldState::~WorldState() {
    free(world);
    cudaFree(d_curr);
    cudaFree(d_next);
}

size_t WorldState::index(size_t x, size_t y, size_t z) const {
    return z + k * (y + m * x);
}

void WorldState::update_world(std::size_t tick) {
    const size_t tile_phase = (tick >> 1) & 1; // Partition origin offset: 0 or 1.
    const size_t tiles_x = (n - tile_phase) / 2;
    const size_t tiles_y = (m - tile_phase) / 2;
    const size_t tiles_z = (k - tile_phase) / 2;
    const size_t tile_count = tiles_x * tiles_y * tiles_z;
    const size_t face_phase = tick & 1; // 0: x faces, 1: z faces.

    if (tile_count == 0)
        return;

    const size_t blocks = 1 + (tile_count - 1) / THREADS_PER_BLOCK;
    update_tile<<<blocks, THREADS_PER_BLOCK>>>(
        d_curr, d_next, m, k, tiles_y, tiles_z, tile_count,
        tile_phase, face_phase);
    CUDA_CHECK(cudaGetLastError()); // Checks submission, not GPU completion.
}

__global__ void update_tile(const Cell* d_curr, Cell* d_next,
                           size_t m, size_t k,
                           size_t tiles_y, size_t tiles_z, size_t tile_count,
                           size_t tile_phase, size_t face_phase) {
    const size_t tile_id = size_t(blockIdx.x) * blockDim.x + threadIdx.x;
    if (tile_id >= tile_count)
        return;

    // Coordinates in the tile grid: z changes fastest, then y, then x.
    const size_t tz = tile_id % tiles_z;
    const size_t ty = (tile_id / tiles_z) % tiles_y;
    const size_t tx = tile_id / (tiles_z * tiles_y);

    // Convert the tile-grid coordinates to the first cell's world coordinates.
    const size_t tile_x = TILE_PHASE_SHIFT(tx, tile_phase);
    const size_t tile_y = TILE_PHASE_SHIFT(ty, tile_phase);
    const size_t tile_z = TILE_PHASE_SHIFT(tz, tile_phase);
    const size_t base = tile_z + k * (tile_y + m * tile_x);

    CubeTile tile;
    for (unsigned c = 0; c < 8; ++c) { //TODO: unroll
        const size_t dx = (c >> 2) & 1;
        const size_t dy = (c >> 1) & 1;
        const size_t dz =  c       & 1;

        tile.cells[c] = d_curr[base + (m * k) * dx + k * dy + dz];
    }

    CubeTile next_tile = state_transition(tile, face_phase);

    for (unsigned c = 0; c < 8; ++c) { //TODO: unroll
        const size_t dx = (c >> 2) & 1;
        const size_t dy = (c >> 1) & 1;
        const size_t dz =  c       & 1;

        d_next[base + (m * k) * dx + k * dy + dz] = next_tile.cells[c];
    }
    // TODO: scatter next_tile to d_next, preserve boundary cells, and swap buffers.
    // state_transition currently uses unchanged faces until LUT calls are added.
}


__device__ CubeTile state_transition(CubeTile tile, size_t face_phase) {
    CubeTile next_tile = tile; // Keep reserved state bits initialized.
#ifdef FACE_TRANSITION
    FaceETile face0, face1;

    if (face_phase == 0) { // x faces
        face0 = pack_face(tile, 2, 3, 0, 1);
        face1 = pack_face(tile, 6, 7, 4, 5);
        // face 0 = lookup
        // face 1 = lookup
        unpack_face(face0, next_tile, 2, 3, 0, 1);
        unpack_face(face1, next_tile, 6, 7, 4, 5);
    } else {              // z faces
        face0 = pack_face(tile, 3, 7, 1, 5);
        face1 = pack_face(tile, 2, 6, 0, 4);
        // face 0 = lookup
        // face 1 = lookup
        unpack_face(face0, next_tile, 3, 7, 1, 5);
        unpack_face(face1, next_tile, 2, 6, 0, 4);
    }

#else
    return {};

#endif
    return next_tile;
}
