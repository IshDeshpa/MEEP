#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __CUDACC__
#define MEEP_HD __host__ __device__
#else
#define MEEP_HD
#endif

namespace meep {

// A state is a base-materials encoding of eight voxels in a uint32_t.
// For eight materials this occupies 24 bits, with no sparse nibble indices.
inline uint64_t state_count(unsigned materials) {
    if (materials < 2 || materials > 16)
        throw std::invalid_argument("materials must be between 2 and 16");
    uint64_t states = 1;
    for (unsigned voxel = 0; voxel < 8; ++voxel) states *= materials;
    return states;
}

struct StateSpace {
    uint64_t count;
    uint32_t mask;
};

inline StateSpace state_space(unsigned materials) {
    const uint64_t count = state_count(materials);
    uint64_t enclosing = 1;
    while (enclosing < count) enclosing *= 2;
    return {count, static_cast<uint32_t>(enclosing - 1)};
}

inline uint64_t checked_product(uint64_t a, uint64_t b) {
    if (b && a > std::numeric_limits<uint64_t>::max() / b)
        throw std::invalid_argument("size arithmetic overflow");
    return a * b;
}

inline uint64_t tile_count(uint64_t x, uint64_t y, uint64_t z) {
    if (!x || !y || !z || (x % 2) || (y % 2) || (z % 2))
        throw std::invalid_argument("world dimensions must be positive and even");
    return checked_product(checked_product(x / 2, y / 2), z / 2);
}

inline uint64_t ceil_div(uint64_t value, uint64_t divisor) {
    if (!divisor) throw std::invalid_argument("zero divisor");
    return value / divisor + (value % divisor != 0);
}

MEEP_HD inline uint32_t permutation_step(uint32_t state, uint32_t mask) {
    // A synthetic permutation, NOT material physics. Each operation is
    // invertible on the low state bits, so trajectories do not collapse into
    // one common absorbing state and accidentally shrink the lookup workload.
    state ^= state >> 13;
    state = (state * 0x85ebca6bu + 0x9e3779b9u) & mask;
    state ^= state >> 11;
    state = (state * 0xc2b2ae35u + 0x27d4eb2du) & mask;
    return state;
}

MEEP_HD inline uint32_t transition(uint32_t state, StateSpace space) {
    // Cycle-walk the enclosing power-of-two permutation until the code is in
    // the dense base-material domain. This remains bijective for nonpowers of
    // two, and preserves the original rules for power-of-two material counts.
    // This computation runs during LUT initialization/reference checks only.
    do {
        state = permutation_step(state, space.mask);
    } while (uint64_t{state} >= space.count);
    return state;
}

inline uint32_t initial_state(uint64_t tile, uint64_t seed, StateSpace space) {
    uint64_t value = tile + seed + 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return static_cast<uint32_t>((value ^ (value >> 31)) % space.count);
}

inline uint64_t checksum(const std::vector<uint32_t>& states) {
    uint64_t value = 14695981039346656037ull;
    for (uint32_t state : states) {
        value ^= state;
        value *= 1099511628211ull;
    }
    return value;
}

struct Launch {
    unsigned block_size;
    uint64_t blocks;
    uint64_t threads;
};

inline Launch launch_for(uint64_t requested_workers, unsigned block_size) {
    if (!requested_workers || !block_size)
        throw std::invalid_argument("workers and block size must be positive");
    const unsigned actual_block = static_cast<unsigned>(
        std::min<uint64_t>(requested_workers, block_size));
    const uint64_t blocks = ceil_div(requested_workers, actual_block);
    return {actual_block, blocks, checked_product(blocks, actual_block)};
}

inline std::vector<uint64_t> sweep_workers(uint64_t tiles, uint64_t capacity) {
    if (!tiles || !capacity) throw std::invalid_argument("empty worker sweep");
    std::set<uint64_t> workers{1, tiles};
    for (uint64_t value = 2; value < tiles;) {
        workers.insert(value);
        if (value > tiles / 2) break;
        value *= 2;
    }
    for (unsigned divisor : {4u, 2u, 1u})
        workers.insert(std::min(tiles, std::max(uint64_t{1}, capacity / divisor)));
    for (unsigned multiplier : {2u, 4u, 8u})
        workers.insert(capacity > tiles / multiplier ? tiles : capacity * multiplier);
    return {workers.begin(), workers.end()};
}

inline double percentile(std::vector<double> values, double fraction) {
    if (values.empty() || fraction < 0 || fraction > 1)
        throw std::invalid_argument("invalid percentile");
    std::sort(values.begin(), values.end());
    const double position = fraction * (values.size() - 1);
    const auto lower = static_cast<size_t>(position);
    const auto upper = std::min(lower + 1, values.size() - 1);
    return values[lower] + (values[upper] - values[lower]) * (position - lower);
}

}  // namespace meep

#undef MEEP_HD
