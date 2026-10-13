#include "benchmark_common.h"

#include <iostream>
#include <numeric>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Function>
void rejects(Function function) {
    bool rejected = false;
    try { function(); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid input was accepted");
}
}  // namespace

int main() {
    try {
        require(meep::tile_count(100, 100, 100) == 125000, "world tile count");
        require(meep::state_count(8) * 4 == 67108864, "8-material LUT size");
        require(meep::state_count(10) * 4 == 400000000, "10-material LUT size");
        require(meep::state_count(12) * 4 == 1719926784, "12-material LUT size");
        require(meep::state_count(14) * 4 == 5903156224ull, "14-material LUT size");
        require(meep::state_count(15) * 4 == 10251562500ull, "15-material LUT size");
        require(meep::state_count(16) * 4 == 17179869184ull, "16-material LUT size");
        rejects([] { meep::tile_count(99, 100, 100); });
        rejects([] { meep::tile_count(0, 2, 2); });
        rejects([] { meep::tile_count(UINT64_MAX - 1, UINT64_MAX - 1, 2); });
        rejects([] { meep::state_count(1); });
        rejects([] { meep::state_count(17); });
        rejects([] { meep::launch_for(0, 256); });
        rejects([] { meep::checked_product(UINT64_MAX, 2); });

        // Exhaustively verify the small rule table is a permutation. This
        // catches accidental state-space collapse or an out-of-range LUT result.
        for (unsigned materials : {2u, 3u, 4u, 5u}) {
            const auto space = meep::state_space(materials);
            const uint64_t states = space.count;
            std::vector<bool> seen(states, false);
            for (uint64_t state = 0; state < states; ++state) {
                const uint32_t next = meep::transition(state, space);
                require(next < states && !seen[next], "transition is not a permutation");
                seen[next] = true;
            }
        }
        for (unsigned materials : {8u, 10u, 12u, 14u, 15u, 16u}) {
            const auto space = meep::state_space(materials);
            for (uint64_t tile = 0; tile < 10000; ++tile) {
                const auto state = meep::initial_state(tile, 42, space);
                require(state < space.count, "initial state out of range");
                require(meep::transition(state, space) < space.count, "transition out of range");
                if ((materials & (materials - 1)) == 0)
                    require(meep::transition(state, space) == meep::permutation_step(state, space.mask),
                            "power-of-two baseline rules changed");
            }
        }

        // Model the kernel scheduling on the host, including rounded grids,
        // partial final blocks, oversubscribed grids, and odd/even ping-pong.
        const uint64_t tiles = 137;
        const auto space = meep::state_space(3);
        std::vector<uint32_t> initial(tiles);
        for (uint64_t i = 0; i < tiles; ++i) initial[i] = meep::initial_state(i, 42, space);
        for (unsigned block : {1u, 32u, 128u, 256u}) {
            for (uint64_t workers : {1ull, 2ull, 31ull, 32ull, 33ull, 136ull, 137ull, 512ull}) {
                const auto launch = meep::launch_for(workers, block);
                std::vector<unsigned> writers(tiles, 0);
                for (uint64_t thread = 0; thread < launch.threads; ++thread)
                    for (uint64_t i = thread; i < tiles; i += launch.threads) ++writers[i];
                require(std::all_of(writers.begin(), writers.end(),
                                    [](unsigned n) { return n == 1; }), "tile coverage");
                for (unsigned ticks : {1u, 2u, 3u, 10u}) {
                    auto current = initial;
                    std::vector<uint32_t> next(tiles);
                    auto expected = initial;
                    for (unsigned tick = 0; tick < ticks; ++tick) {
                        for (uint64_t thread = 0; thread < launch.threads; ++thread)
                            for (uint64_t i = thread; i < tiles; i += launch.threads)
                                next[i] = meep::transition(current[i], space);
                        current.swap(next);
                        for (auto& state : expected) state = meep::transition(state, space);
                    }
                    require(current == expected, "grid-stride ping-pong result");
                    require(meep::checksum(current) == meep::checksum(expected), "checksum");
                }
            }
        }
        const auto sweep = meep::sweep_workers(125000, 36864);
        for (uint64_t point : {1ull, 9216ull, 18432ull, 36864ull, 73728ull, 125000ull})
            require(std::binary_search(sweep.begin(), sweep.end(), point), "missing sweep point");
        require(meep::launch_for(1, 256).threads == 1, "serial launch is not serial");
        require(meep::launch_for(125000, 256).threads == 125184, "grid rounding");
        require(meep::percentile({9, 1, 5}, 0.5) == 5, "median");
        require(meep::percentile({2, 4}, 0.5) == 3, "even median");
        std::cout << "Host checks passed: sizes, rule range/permutation, scheduling, buffer parity, statistics.\n";
    } catch (const std::exception& error) {
        std::cerr << "Host check failed: " << error.what() << '\n';
        return 1;
    }
}
