#include "state/state.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MEEP_SRC_DIR
#define MEEP_SRC_DIR "src"
#endif

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

using TileGrid = std::array<size_t, 3>;

size_t tileCount(const TileGrid& grid) {
    return grid[0] * grid[1] * grid[2];
}

size_t cellIndex(const TileGrid& padded, size_t x, size_t y, size_t z) {
    return (x * padded[1] + y) * padded[2] + z;
}

// Check every pair of material IDs and every state nibble in each cell position.
__host__ __device__
bool equalityCase(unsigned case_id) {
    const unsigned position = case_id & 7;
    const unsigned element = (case_id >> 3) & 15;
    const unsigned state = (case_id >> 7) & 15;
    const unsigned base_element = (case_id >> 11) & 15;
    CubeTile a{}, b{};
    for (unsigned c = 0; c < 8; ++c) {
        a.cells[c] = Cell{static_cast<Element>(base_element), static_cast<uint8_t>(c)};
        b.cells[c] = Cell{static_cast<Element>(base_element), static_cast<uint8_t>((c + state) & 15)};
    }
    if (!(a == b) || a != b) return false;
    b.cells[position].elem = static_cast<Element>(element);
    const bool equal = element == base_element;
    return (a == b) == equal && (a != b) == !equal;
}

__global__ void checkEquality(unsigned* failures) {
    const unsigned case_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (case_id < 32768 && !equalityCase(case_id)) atomicAdd(failures, 1u);
}

void checkEquality() {
    for (unsigned case_id = 0; case_id < 32768; ++case_id)
        require(equalityCase(case_id), "Host material-only equality failed");
    unsigned* failures = nullptr;
    CUDA_CHECK(cudaMalloc(&failures, sizeof(unsigned)));
    try {
        CUDA_CHECK(cudaMemset(failures, 0, sizeof(unsigned)));
        checkEquality<<<128, 256>>>(failures);
        CUDA_CHECK(cudaGetLastError());
        unsigned result;
        CUDA_CHECK(cudaMemcpy(&result, failures, sizeof(result), cudaMemcpyDeviceToHost));
        require(result == 0, "GPU material-only equality failed");
    } catch (...) {
        cudaFree(failures);
        throw;
    }
    CUDA_CHECK(cudaFree(failures));
    std::cout << "PASS: 32768 material/state equality cases on CPU and GPU\n";
}

std::vector<FaceETile> makeRules(unsigned mode) {
    std::vector<FaceETile> rules(FACE_LUT_ELEMS);
    for (unsigned key = 0; key < rules.size(); ++key) {
        unsigned cells[4] = {key >> 12, (key >> 8) & 15, (key >> 4) & 15, key & 15};
        if (mode == 1) {
            // Synthetic gravity: WATER falls down either column of a face.
            for (unsigned c = 0; c < 2; ++c) {
                if (cells[c] == 1 && cells[c + 2] == 0) {
                    cells[c] = 0;
                    cells[c + 2] = 1;
                }
            }
        } else if (mode == 2) {
            // Reversible horizontal swaps exercise x/z reversals and pair flags.
            for (unsigned c = 0; c < 4; c += 2) {
                if (cells[c] != 4 && cells[c + 1] != 4) {
                    const unsigned saved = cells[c];
                    cells[c] = cells[c + 1];
                    cells[c + 1] = saved;
                }
            }
        } else if (mode == 3) {
            // A material may keep changing on its own next partition visit.
            for (unsigned& cell : cells)
                if (cell >= 1 && cell <= 3) cell = cell % 3 + 1;
        }
        rules[key] = static_cast<FaceETile>((cells[0] << 12) | (cells[1] << 8) |
                                          (cells[2] << 4) | cells[3]);
    }
    return rules;
}

// Derive overlap from the eight actual cell coordinates, independently of the
// GPU's {-1,0}/{0,1} neighbor-coordinate loops.
bool referenceActive(const TileGrid& padded, size_t phase, size_t tx, size_t ty,
                     size_t tz, const std::array<std::vector<bool>, 2>& changes) {
    const TileGrid grid{(padded[0] - phase) / 2, (padded[1] - phase) / 2,
                        (padded[2] - phase) / 2};
    const size_t tile_id = (tx * grid[1] + ty) * grid[2] + tz;
    if (changes[phase][tile_id]) return true;
    const size_t other = phase ^ 1;
    const TileGrid prev{(padded[0] - other) / 2, (padded[1] - other) / 2,
                        (padded[2] - other) / 2};
    for (unsigned c = 0; c < 8; ++c) {
        const size_t x = 2 * tx + phase + ((c >> 2) & 1);
        const size_t y = 2 * ty + phase + ((c >> 1) & 1);
        const size_t z = 2 * tz + phase + (c & 1);
        if (x < other || y < other || z < other) continue;
        const size_t px = (x - other) / 2, py = (y - other) / 2, pz = (z - other) / 2;
        if (px >= prev[0] || py >= prev[1] || pz >= prev[2]) continue;
        if (changes[other][(px * prev[1] + py) * prev[2] + pz]) return true;
    }
    return false;
}

#if DIRTY_STATE_EXCLUDE_ONLY
void checkBitmap(const uint32_t* bitmap, const std::vector<bool>& expected,
                 const std::string& label) {
    std::vector<uint32_t> words((expected.size() + 31) / 32);
    CUDA_CHECK(cudaMemcpy(words.data(), bitmap, words.size() * sizeof(uint32_t),
                          cudaMemcpyDeviceToHost));
    for (size_t i = 0; i < words.size() * 32; ++i) {
        const bool actual = (words[i >> 5] & (1u << (i & 31))) != 0;
        require(actual == (i < expected.size() && expected[i]),
                label + ": wrong flag at " + std::to_string(i));
    }
}

#endif

void checkWorld(size_t n, size_t m, size_t k, unsigned mode) {
    const TileGrid padded{n + 2, m + 2, k + 2};
    const std::array<TileGrid, 2> grids{{
        {padded[0] / 2, padded[1] / 2, padded[2] / 2},
        {(padded[0] - 1) / 2, (padded[1] - 1) / 2, (padded[2] - 1) / 2}}};
    const size_t cell_count = padded[0] * padded[1] * padded[2];
    const size_t world_bytes = cell_count * sizeof(Cell);
    size_t bitmap_bytes = 0;
#if DIRTY_STATE_EXCLUDE_ONLY
    bitmap_bytes = ((tileCount(grids[0]) + 31) / 32) * sizeof(uint32_t);
    for (const auto& grid : grids) bitmap_bytes += ((tileCount(grid) + 31) / 32) * sizeof(uint32_t);
#endif
    const size_t required_bytes = FACE_LUT_SIZE + 2 * world_bytes + bitmap_bytes;
    size_t free_bytes, total_bytes;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    std::cout << n << 'x' << m << 'x' << k << " rules=" << mode
              << ": estimated bytes=" << required_bytes << " (bitmaps=" << bitmap_bytes
              << "), free=" << free_bytes << ", used=" << total_bytes - free_bytes << '\n';
    require(required_bytes <= free_bytes, "Insufficient VRAM for test case");
    const auto rules = makeRules(mode);
    LUT lut((std::filesystem::path(MEEP_SRC_DIR) / "state/test1.bin").c_str());
    CUDA_CHECK(cudaMemcpy(lut.d_lut, rules.data(), FACE_LUT_SIZE, cudaMemcpyHostToDevice));
    WorldState world(n, m, k);
    std::vector<Cell> current(world.world, world.world + cell_count);
    for (size_t x = 1; x <= n; ++x) {
        for (size_t y = 1; y <= m; ++y) {
            for (size_t z = 1; z <= k; ++z) {
                const size_t i = cellIndex(padded, x, y, z);
                const unsigned hash = unsigned((x * 73856093) ^ (y * 19349663) ^ (z * 83492791));
                // A localized cloud leaves plenty of sleeping tiles around it.
                const bool cloud = x <= 5 && z <= 5 && y > m / 2;
                current[i] = Cell{static_cast<Element>(cloud && hash % 3 == 0 ? 1 + hash % 3 : 0),
                                  static_cast<uint8_t>(i & 15)};
            }
        }
    }
    CUDA_CHECK(cudaMemcpy(world.d_curr, current.data(), world_bytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(world.d_next, current.data(), world_bytes, cudaMemcpyHostToDevice));
    std::array<std::vector<bool>, 2> changes{
        std::vector<bool>(tileCount(grids[0]), true), std::vector<bool>(tileCount(grids[1]), true)};
    std::array<std::vector<bool>, 2> activity{
        std::vector<bool>(tileCount(grids[0])), std::vector<bool>(tileCount(grids[1]))};
    const Cell* original_curr = world.d_curr;
    size_t sleeping = 0;
    for (size_t tick = 0; tick < 32; ++tick) {
        const size_t phase = (tick >> 1) & 1, face = tick & 1;
        const auto& grid = grids[phase];
        if (face == 0) {
            for (size_t tx = 0; tx < grid[0]; ++tx)
                for (size_t ty = 0; ty < grid[1]; ++ty)
                    for (size_t tz = 0; tz < grid[2]; ++tz) {
                        const size_t id = (tx * grid[1] + ty) * grid[2] + tz;
                        activity[phase][id] = referenceActive(padded, phase, tx, ty, tz, changes);
                    }
            changes[phase].assign(tileCount(grid), false);
        }
        // Full CPU update: always apply both face rules, irrespective of activity.
        auto next = current;
        for (size_t tx = 0; tx < grid[0]; ++tx) {
            for (size_t ty = 0; ty < grid[1]; ++ty) {
                for (size_t tz = 0; tz < grid[2]; ++tz) {
                    const size_t id = (tx * grid[1] + ty) * grid[2] + tz;
                    sleeping += !activity[phase][id];
                    size_t indices[8];
                    for (unsigned c = 0; c < 8; ++c)
                        indices[c] = cellIndex(padded, 2 * tx + phase + ((c >> 2) & 1),
                                               2 * ty + phase + ((c >> 1) & 1),
                                               2 * tz + phase + (c & 1));
                    const unsigned x_faces[2][4] = {{2, 3, 0, 1}, {6, 7, 4, 5}};
                    const unsigned z_faces[2][4] = {{3, 7, 1, 5}, {2, 6, 0, 4}};
                    for (unsigned f = 0; f < 2; ++f) {
                        const auto& corners = face == 0 ? x_faces[f] : z_faces[f];
                        unsigned key = 0;
                        for (unsigned c : corners)
                            key = (key << 4) | unsigned(current[indices[c]].elem);
                        const unsigned result = rules[key];
                        for (unsigned c = 0; c < 4; ++c)
                            next[indices[corners[c]]].elem = static_cast<Element>((result >> (12 - 4 * c)) & 15);
                    }
                    bool changed = false;
                    for (size_t i : indices) changed |= current[i].elem != next[i].elem;
                    changes[phase][id] = changes[phase][id] || changed;
                }
            }
        }
        // Poison visible destinations to catch missing sleeping-tile carry-forward.
        auto poisoned = current;
        for (size_t x = 1; x <= n; ++x)
            for (size_t y = 1; y <= m; ++y)
                for (size_t z = 1; z <= k; ++z)
                    poisoned[cellIndex(padded, x, y, z)] = Cell{static_cast<Element>(15), 15};
        CUDA_CHECK(cudaMemcpy(world.d_next, poisoned.data(), world_bytes, cudaMemcpyHostToDevice));
        if ((tick & 1) == 0) world.updateActiveTiles(tick);
        world.updateWorld(tick, lut);
        world.waitForTick();
        std::vector<Cell> actual(cell_count);
        CUDA_CHECK(cudaMemcpy(actual.data(), world.d_next, world_bytes, cudaMemcpyDeviceToHost));
        for (size_t i = 0; i < cell_count; ++i)
            require(actual[i].elem == next[i].elem && actual[i].state == next[i].state,
                    "CPU/GPU mismatch tick=" + std::to_string(tick) + " cell=" + std::to_string(i));
#if DIRTY_STATE_EXCLUDE_ONLY
        checkBitmap(world.d_active,
                    activity[phase], "activity tick=" + std::to_string(tick));
        checkBitmap(phase == 0 ? world.d_phase0_dirty_next : world.d_phase1_dirty_next,
                    changes[phase], "changes tick=" + std::to_string(tick));
#endif
        world.swapBuffers();
        require((world.d_curr == original_curr) == ((tick & 1) != 0), "Wrong world buffer parity");
        current = std::move(next);
    }
    if (mode == 0) require(sleeping != 0, "Identity fixture did not exercise sleeping tiles");
    if (mode == 0)
        for (const auto& map : changes)
            for (bool changed : map) require(!changed, "Identity scene did not settle");
    std::cout << "PASS: 32 ticks against full CPU reference; reference sleeping tile visits=" << sleeping << '\n';
}
} // namespace

int main() {
    try {
        checkEquality();
        const size_t sizes[][3] = {{1, 1, 1}, {3, 4, 5}, {6, 6, 6}, {9, 8, 7}, {32, 18, 14}};
        for (unsigned mode = 0; mode < 4; ++mode)
            for (const auto& size : sizes) checkWorld(size[0], size[1], size[2], mode);
        std::cout << "PASS: dirty scheduling, bitmap tails, phase overlap, state bits, and world parity\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
