#pragma once

#include "transitions.h"
#include <thread>

namespace tv {

// Tiles have no inter-tile dependencies in this fixed-partition experiment.
// Parallelize only the untimed CPU reference, without allocating extra worlds.
inline void reference_world(std::vector<uint32_t>& states, unsigned materials,
                            Shape kind, bool sequential, uint64_t ticks,
                            unsigned workers = 8) {
    meep::state_count(materials);
    if (!workers) throw std::invalid_argument("reference workers must be positive");
    workers = static_cast<unsigned>(std::min<uint64_t>(workers, states.size()));
    if (!workers) return;
    auto chunk = [&](unsigned worker) {
        const uint64_t begin = states.size() * worker / workers;
        const uint64_t end = states.size() * (worker + 1) / workers;
        for (uint64_t tile = begin; tile < end; ++tile) {
            uint32_t state = states[tile];
            for (uint64_t tick = 0; tick < ticks; ++tick)
                state = reference_update(state, materials, kind, sequential);
            states[tile] = state;
        }
    };
    std::vector<std::thread> threads;
    try {
        for (unsigned worker = 1; worker < workers; ++worker) threads.emplace_back(chunk, worker);
    } catch (...) {
        for (auto& thread : threads) thread.join();
        throw;
    }
    chunk(0);
    for (auto& thread : threads) thread.join();
}

}  // namespace tv
