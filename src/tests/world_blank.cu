#include "state/state.hpp"
#include "extra/check.hpp"

#include <cuda_runtime.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef MEEP_SRC_DIR
#define MEEP_SRC_DIR "src"
#endif

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<FaceETile> readRules(const std::filesystem::path& file_path) {
    require(std::filesystem::file_size(file_path) == FACE_LUT_SIZE,
            "Incorrect LUT file size");
    std::ifstream file(file_path, std::ios::binary);
    std::vector<FaceETile> rules(FACE_LUT_ELEMS);
    require(bool(file.read(reinterpret_cast<char*>(rules.data()), FACE_LUT_SIZE)),
            "Failed to read CPU reference LUT");
    // Every possible AIR/BLOCK face must preserve an otherwise blank world.
    for (unsigned mask = 0; mask < 16; ++mask) {
        FaceETile face = 0;
        for (unsigned cell = 0; cell < 4; ++cell) {
            const auto material = (mask & (1u << cell)) ? Element::BLOCK : Element::AIR;
            face |= static_cast<FaceETile>(static_cast<unsigned>(material) << (4 * cell));
        }
        require(rules[face] == face, "LUT changes an AIR/BLOCK reference face");
    }
    return rules;
}

void checkBuffer(const Cell* buffer, const std::vector<Cell>& expected,
                 const std::string& label) {
    std::vector<Cell> actual(expected.size());
    CUDA_CHECK(cudaMemcpy(actual.data(), buffer, actual.size() * sizeof(Cell),
                          cudaMemcpyDeviceToHost));
    for (size_t cell = 0; cell < actual.size(); ++cell) {
        require(actual[cell].elem == expected[cell].elem &&
                    actual[cell].state == expected[cell].state,
                label + ": cell " + std::to_string(cell) + " changed");
    }
}

void checkWorld(size_t n, size_t m, size_t k, const std::string& file_path,
                const std::vector<FaceETile>& rules) {
    constexpr size_t ticks = 7; // Both orientations/partitions, odd final parity.
    const size_t count = (n + 2) * (m + 2) * (k + 2);
    const size_t buffer_bytes = count * sizeof(Cell);
    const size_t required_bytes = FACE_LUT_SIZE + 2 * buffer_bytes;
    size_t free_bytes = 0, total_bytes = 0;
    CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
    std::cout << n << 'x' << m << 'x' << k << ": LUT=" << FACE_LUT_SIZE
              << ", two buffers=" << 2 * buffer_bytes
              << ", total required=" << required_bytes
              << ", free=" << free_bytes << ", used=" << total_bytes - free_bytes
              << " bytes\n";
    require(required_bytes <= free_bytes, "Insufficient free VRAM for case");

    LUT lut(file_path.c_str());
    WorldState world(n, m, k);
    std::vector<FaceETile> uploaded(rules.size());
    CUDA_CHECK(cudaMemcpy(uploaded.data(), lut.deviceView().lut, FACE_LUT_SIZE,
                          cudaMemcpyDeviceToHost));
    require(uploaded == rules, "Uploaded GPU LUT differs from file");

    // Independent CPU reference: AIR interior, BLOCK shell, zero state bits.
    std::vector<Cell> expected(count);
    auto poisoned = expected;
    for (size_t x = 0; x < n + 2; ++x) {
        for (size_t y = 0; y < m + 2; ++y) {
            for (size_t z = 0; z < k + 2; ++z) {
                const size_t index = (x * (m + 2) + y) * (k + 2) + z;
                const bool border = x == 0 || x == n + 1 || y == 0 ||
                                    y == m + 1 || z == 0 || z == k + 1;
                expected[index] = Cell{border ? Element::BLOCK : Element::AIR, 0};
                poisoned[index] = border ? expected[index]
                    : Cell{static_cast<Element>(15), 15};
            }
        }
    }
    require(std::memcmp(world.world, expected.data(), buffer_bytes) == 0,
            "CPU world did not initialize blank");
    checkBuffer(world.d_curr, expected, "Initial current buffer");
    checkBuffer(world.d_next, expected, "Initial next buffer");
    const Cell* original_current = world.d_curr;
    for (size_t tick = 0; tick < ticks; ++tick) {
        // Expose skipped output writes; source world stays entirely blank.
        CUDA_CHECK(cudaMemcpy(world.d_next, poisoned.data(), buffer_bytes,
                              cudaMemcpyHostToDevice));
        world.updateWorld(tick, lut);
        world.waitForTick();
        checkBuffer(world.d_next, expected, "Output after tick " + std::to_string(tick));
        checkBuffer(world.d_curr, expected, "Input after tick " + std::to_string(tick));
        // Exercise the production swap after GPU completion.
        world.swapBuffers();
        require((world.d_curr == original_current) == ((tick & 1) != 0),
                "Incorrect buffer parity");
    }
    std::cout << "PASS: " << ticks << " ticks; AIR interior, BLOCK shell, zero state; "
              << "all output cells written; explicit swapBuffers calls\n";
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        require(argc <= 2, "Usage: world_blank [LUT path]");
        const std::filesystem::path file_path = argc == 2
            ? std::filesystem::path(argv[1])
            : std::filesystem::path(MEEP_SRC_DIR) / "state/la.bin";
        const auto rules = readRules(file_path);
        const size_t sizes[][3] = {{1, 1, 1}, {3, 4, 5}, {10, 14, 18}, {100, 100, 100}};
        for (const auto& size : sizes)
            checkWorld(size[0], size[1], size[2], file_path.string(), rules);
        std::cout << "PASS: blank-world GPU regression with the loaded material LUT.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
