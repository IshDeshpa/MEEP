#include "state/state.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<Cell> download(const Cell* buffer, size_t count) {
    std::vector<Cell> result(count);
    CUDA_CHECK(cudaMemcpy(result.data(), buffer, count * sizeof(Cell),
                          cudaMemcpyDeviceToHost));
    return result;
}

__global__ void exercise_tile_view(DeviceWorldView buffers,
                                   std::ptrdiff_t x, std::ptrdiff_t y,
                                   std::ptrdiff_t z) {
    CubeTile tile = buffers.readTile(x, y, z);
    for (unsigned c = 0; c < 8; ++c) {
        tile.cells[c].elem = static_cast<Element>(
            (static_cast<unsigned>(tile.cells[c].elem) + 1) & 15);
        tile.cells[c].state = (tile.cells[c].state + c + 1) & 15;
    }
    buffers.writeTile(x, y, z, tile);
}

void check_tile_helpers(WorldState& world, const std::vector<Cell>& source) {
    const std::ptrdiff_t origins[][3] = {
        {-1, -1, -1}, {0, 0, 0},
        {static_cast<std::ptrdiff_t>(world.n) - 1,
         static_cast<std::ptrdiff_t>(world.m) - 1,
         static_cast<std::ptrdiff_t>(world.k) - 1}};
    const size_t bytes = source.size() * sizeof(Cell);
    for (const auto& origin : origins) {
        auto expected = source;
        for (size_t dx = 0; dx < 2; ++dx)
            for (size_t dy = 0; dy < 2; ++dy)
                for (size_t dz = 0; dz < 2; ++dz) {
                    const size_t x = static_cast<size_t>(origin[0] + 1) + dx;
                    const size_t y = static_cast<size_t>(origin[1] + 1) + dy;
                    const size_t z = static_cast<size_t>(origin[2] + 1) + dz;
                    const size_t address = (x * (world.m + 2) + y) * (world.k + 2) + z;
                    const unsigned c = static_cast<unsigned>(4 * dx + 2 * dy + dz);
                    expected[address].elem = static_cast<Element>(
                        (static_cast<unsigned>(source[address].elem) + 1) & 15);
                    expected[address].state = (source[address].state + c + 1) & 15;
                }

        CUDA_CHECK(cudaMemcpy(world.d_next, source.data(), bytes,
                              cudaMemcpyHostToDevice));
        exercise_tile_view<<<1, 1>>>(world.deviceView(),
                                     origin[0], origin[1], origin[2]);
        CUDA_CHECK(cudaGetLastError());
        world.waitForTick();
        const auto actual = download(world.d_next, source.size());
        require(std::memcmp(actual.data(), expected.data(), bytes) == 0,
                "Tile helper used wrong coordinates, touched other cells, or filtered BLOCK");
    }
}

void check_world(size_t n, size_t m, size_t k) {
    const size_t count = (n + 2) * (m + 2) * (k + 2);
    const size_t bytes = count * sizeof(Cell);
    size_t free_bytes, total_bytes;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    std::cout << n << 'x' << m << 'x' << k
              << ": LUT bytes=0 (identity placeholder), two padded buffers="
              << 2 * bytes << ", free=" << free_bytes
              << ", used=" << total_bytes - free_bytes << '\n';
    require(bytes <= free_bytes / 2, "Not enough free GPU memory for test");

    WorldState world(n, m, k);
    require(world.n == n && world.m == m && world.k == k,
            "Visible dimensions changed");
    require(world.n_pad == n + 2 && world.m_pad == m + 2 &&
            world.k_pad == k + 2, "Wrong padded dimensions");

    const WorldState& const_world = world;
    for (const coord c : {coord(-1, 0, 0), coord(0, -1, 0), coord(0, 0, -1),
                         coord(static_cast<int>(n), 0, 0),
                         coord(0, static_cast<int>(m), 0),
                         coord(0, 0, static_cast<int>(k))}) {
        require(const_world(c).elem == Element::BLOCK,
                "Renderer neighbor access did not reach the BLOCK halo");
    }
    require(&world(coord(0, 0, 0)) == world.world + world.index(0, 0, 0),
            "Renderer access did not use visible coordinates");
    bool rejected_coordinate = false;
    try { (void)world(coord(-2, 0, 0)); }
    catch (const std::out_of_range&) { rejected_coordinate = true; }
    require(rejected_coordinate, "Renderer coordinate outside padding accepted");

    std::vector<Cell> expected(count);
    for (size_t x = 0; x < n + 2; ++x) {
        for (size_t y = 0; y < m + 2; ++y) {
            for (size_t z = 0; z < k + 2; ++z) {
                const size_t address = ((x * (m + 2)) + y) * (k + 2) + z;
                const bool border = x == 0 || x == n + 1 ||
                                    y == 0 || y == m + 1 ||
                                    z == 0 || z == k + 1;
                require(world.paddedIndex(x, y, z) == address,
                        "Padded indexing mismatch");
                expected[address] = Cell{border ? Element::BLOCK : Element::AIR, 0};
                if (!border)
                    require(world.index(x - 1, y - 1, z - 1) == address,
                            "Visible coordinates do not map inside padding");
            }
        }
    }

    require(std::memcmp(world.world, expected.data(), bytes) == 0,
            "CPU initialization image is wrong");
    for (const Cell* buffer : {world.d_curr, world.d_next}) {
        const auto actual = download(buffer, count);
        require(std::memcmp(actual.data(), expected.data(), bytes) == 0,
                "A GPU buffer was not initialized with AIR and BLOCK padding");
    }

    // Distinct interior values expose bad strides and accidental boundary copies.
    // These expected outputs describe the current identity LUT placeholder.
    for (size_t x = 0; x < n; ++x)
        for (size_t y = 0; y < m; ++y)
            for (size_t z = 0; z < k; ++z) {
                const size_t visible = (x * m + y) * k + z;
                expected[world.index(x, y, z)] = Cell{
                    static_cast<Element>(visible % 7),
                    static_cast<uint8_t>((visible * 5 + 1) & 15)};
            }
    CUDA_CHECK(cudaMemcpy(world.d_curr, expected.data(), bytes,
                          cudaMemcpyHostToDevice));

    check_tile_helpers(world, expected);

    const Cell* original_current = world.d_curr;
    // Seven ticks cover x/z in both partitions and finish on odd buffer parity.
    for (size_t tick = 0; tick < 7; ++tick) {
        auto poisoned = expected;
        for (size_t x = 0; x < n; ++x)
            for (size_t y = 0; y < m; ++y)
                for (size_t z = 0; z < k; ++z)
                    poisoned[world.index(x, y, z)] =
                        Cell{static_cast<Element>(15), 15};
        // Padding in this destination retains its initialized BLOCK value.
        CUDA_CHECK(cudaMemcpy(world.d_next, poisoned.data(), bytes,
                              cudaMemcpyHostToDevice));
        world.updateWorld(tick);
        world.waitForTick();
        const auto actual = download(world.d_next, count);
        require(std::memcmp(actual.data(), expected.data(), bytes) == 0,
                "Tick missed an interior cell, changed padding, or misindexed cells");

        // Production buffer-role management is still a separate unfinished step.
        std::swap(world.d_curr, world.d_next);
        require((world.d_curr == original_current) == ((tick & 1) != 0),
                "Unexpected buffer parity");
    }
}

void check_invalid_dimensions() {
    bool rejected = false;
    try { WorldState world(0, 1, 1); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Zero dimension accepted");

    const size_t max = std::numeric_limits<size_t>::max();
    for (const size_t n : {max, max - 1, max - 2}) {
        rejected = false;
        try { WorldState world(n, 2, 2); }
        catch (const std::overflow_error&) { rejected = true; }
        require(rejected, "Padding or allocation overflow accepted");
    }
}
} // namespace

int main() {
    try {
        check_invalid_dimensions();
        const size_t sizes[][3] = {
            {1, 1, 1}, {2, 2, 2}, {3, 4, 5},
            {4, 5, 6}, {8, 6, 4}, {10, 14, 18}};
        for (const auto& size : sizes) {
            check_world(size[0], size[1], size[2]);
        }
        std::cout << "PASS: padding, initialization, visible indexing, both partitions, "
                     "partial blocks, coordinate-based tile helpers, LUT-owned BLOCK behavior, and buffer parity.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
