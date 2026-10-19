#include "transitions.h"
#include "reference_world.h"

#include <iostream>

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

struct RuleLookup {
    meep::StateSpace space;
    uint32_t operator()(uint32_t key) const {
        require(key < space.count, "LUT key out of range");
        return meep::transition(key, space);
    }
};

// Stamp every proposed voxel with its part number. Captured keys expose
// whether subsequent lookups read the original or the overwritten tile.
template <tv::Shape Kind>
struct StampLookup {
    std::vector<uint32_t>& keys;
    uint32_t operator()(uint32_t key) const {
        keys.push_back(key);
        uint32_t code = 0;
        for (unsigned p = 0; p < tv::Traits<Kind>::cells; ++p)
            code = code * 8 + uint32_t(keys.size() % 8);
        return code;
    }
};

template <tv::Shape Kind>
void check_overwrites() {
    // The original tile's voxel i contains material i.
    uint32_t original = 0;
    for (unsigned cell = 8; cell-- > 0;) original = original * 8 + cell;
    std::vector<uint32_t> independent_keys, sequential_keys;
    const auto independent = tv::update_tile<Kind, false>(original, 8, StampLookup<Kind>{independent_keys});
    const auto sequential = tv::update_tile<Kind, true>(original, 8, StampLookup<Kind>{sequential_keys});
    require(independent_keys.size() == tv::Traits<Kind>::lookups, "independent lookup count");
    require(sequential_keys.size() == tv::Traits<Kind>::lookups, "sequential lookup count");
    require(independent_keys != sequential_keys, "lookup inputs ignore the update mode");
    for (unsigned part = 0; part < tv::Traits<Kind>::lookups; ++part) {
        uint32_t expected_key = 0, place = 1;
        for (unsigned p = 0; p < tv::Traits<Kind>::cells; ++p) {
            expected_key += tv::part_cell<Kind>(part, p) * place;
            place *= 8;
        }
        require(independent_keys[part] == expected_key, "independent lookup read temporary state");
    }
    uint32_t expected = 0;
    for (unsigned cell = 8; cell-- > 0;) {
        // Last two faces cover the z planes; last four edges run along z.
        const unsigned value = Kind == tv::Shape::face ? (cell < 4 ? 5 : 6) : (cell % 4 + 1);
        expected = expected * 8 + value;
    }
    require(independent == expected && sequential == expected, "fixed-order overwrite result");
}

template <tv::Shape Kind, bool Sequential>
void check_transitions() {
    for (unsigned materials : {2u, 3u, 8u, 10u, 12u, 14u, 16u}) {
        const auto world = meep::state_space(materials);
        const auto space = tv::state_space(materials, tv::Traits<Kind>::cells);
        bool differs_from_other_mode = false;
        // Exhaustive binary cubes; seeded samples for larger material domains.
        for (uint64_t i = 0; i < 512; ++i) {
            const uint32_t state = materials == 2 ? i % 256 : meep::initial_state(i, 42, world);
            const auto result = tv::update_tile<Kind, Sequential>(state, materials, RuleLookup{space});
            const auto expected = tv::reference_update(state, materials, Kind, Sequential);
            require(result == expected, "update differs from independent CPU implementation");
            require(result < world.count, "next tile out of range");
            if constexpr (Kind != tv::Shape::cube)
                differs_from_other_mode |= result != tv::reference_update(state, materials, Kind, !Sequential);
            if constexpr (Kind == tv::Shape::cube)
                require(result == meep::transition(state, world), "cube baseline rules changed");
        }
        if constexpr (Kind != tv::Shape::cube)
            require(differs_from_other_mode, "sequential and independent unexpectedly identical");
    }
}

template <tv::Shape Kind, bool Sequential>
void check_scheduling() {
    using Entry = typename tv::Traits<Kind>::Entry;
    constexpr uint64_t tiles = 137;
    const unsigned materials = 3;
    const auto space = tv::state_space(materials, tv::Traits<Kind>::cells);
    std::vector<Entry> table(space.count);
    for (uint64_t key = 0; key < space.count; ++key)
        table[key] = static_cast<Entry>(meep::transition(static_cast<uint32_t>(key), space));
    const tv::TableLookup<Entry, Kind != tv::Shape::cube && !Sequential> lookup{table.data()};
    std::vector<uint32_t> initial(tiles);
    for (uint64_t tile = 0; tile < tiles; ++tile)
        initial[tile] = meep::initial_state(tile, 42, meep::state_space(materials));
    for (unsigned block : {1u, 32u, 128u, 256u}) {
        const uint64_t blocks = meep::ceil_div(tiles, block);
        const meep::Launch launch{block, blocks, blocks * block};
        std::vector<unsigned> writers(tiles, 0);
        for (uint64_t thread = 0; thread < launch.threads; ++thread)
            if (thread < tiles) ++writers[thread];
        require(std::all_of(writers.begin(), writers.end(), [](unsigned n) { return n == 1; }),
                "scheduling must give every output tile exactly one writer");
        for (unsigned warmup : {0u, 1u, 2u}) {
            for (unsigned ticks : {1u, 2u, 3u}) {
                auto a = initial;
                std::vector<uint32_t> b(tiles, UINT32_MAX);
                auto* current = &a;
                auto* next = &b;
                auto expected = initial;
                for (unsigned tick = 0; tick < warmup + ticks; ++tick) {
                    for (uint64_t tile = 0; tile < launch.threads; ++tile)
                        if (tile < tiles)
                            (*next)[tile] = tv::update_tile<Kind, Sequential>((*current)[tile], materials, lookup);
                    std::swap(current, next);
                    for (auto& state : expected)
                        state = tv::reference_update(state, materials, Kind, Sequential);
                }
                require(*current == expected, "scheduling or ping-pong result differs from reference");
                require(current == ((warmup + ticks) % 2 ? &b : &a), "final buffer parity");
            }
        }
    }
}

template <tv::Shape Kind, bool Sequential>
void check_variant() {
    check_transitions<Kind, Sequential>();
    check_scheduling<Kind, Sequential>();
    std::vector<uint32_t> initial(137);
    for (uint64_t i = 0; i < initial.size(); ++i)
        initial[i] = meep::initial_state(i, 42, meep::state_space(3));
    auto expected = initial;
    for (unsigned tick = 0; tick < 3; ++tick)
        for (auto& state : expected) state = tv::reference_update(state, 3, Kind, Sequential);
    for (unsigned workers : {1u, 3u, 8u, 200u}) {
        auto observed = initial;
        tv::reference_world(observed, 3, Kind, Sequential, 3, workers);
        require(observed == expected, "parallel CPU reference differs from serial reference");
    }
}

}  // namespace

int main() {
    try {
        require(meep::tile_count(10, 14, 18) == 315, "world tile count");
        for (unsigned materials : {2u, 3u, 8u, 10u, 12u, 14u, 16u}) {
            const auto cube = tv::state_space(materials, 8);
            const auto face = tv::state_space(materials, 4);
            const auto edge = tv::state_space(materials, 2);
            require(cube.count == meep::state_count(materials), "cube table size");
            require(face.count * face.count == cube.count, "face table size");
            require(edge.count * edge.count == face.count, "edge table size");
            require(face.count - 1 <= UINT16_MAX && edge.count - 1 <= UINT8_MAX, "entry width");
            for (auto space : {face, edge}) {
                std::vector<bool> seen(space.count, false);
                for (uint64_t key = 0; key < space.count; ++key) {
                    const auto result = meep::transition(static_cast<uint32_t>(key), space);
                    require(result < space.count && !seen[result], "subtile rule is not a permutation");
                    seen[result] = true;
                }
            }
        }
        require(tv::state_space(8, 4).count * 2 == 8192, "8-material face bytes");
        require(tv::state_space(14, 2).count == 196, "14-material edge bytes");
        require(tv::state_space(16, 8).count * 4 == 17179869184ull, "16-material cube bytes");
        rejects([] { tv::state_space(1, 4); });
        rejects([] { tv::state_space(17, 2); });
        rejects([] { tv::state_space(8, 3); });
        rejects([] { meep::tile_count(3, 4, 6); });
        rejects([] { meep::tile_count(UINT64_MAX - 1, UINT64_MAX - 1, 2); });
        check_overwrites<tv::Shape::face>();
        check_overwrites<tv::Shape::edge>();
        check_variant<tv::Shape::cube, false>();
        check_variant<tv::Shape::face, false>();
        check_variant<tv::Shape::face, true>();
        check_variant<tv::Shape::edge, false>();
        check_variant<tv::Shape::edge, true>();
        std::cout << "Host checks passed: dense LUTs, lookup inputs/counts, overwrite order, all five variants, "
                     "one thread per tile, and warmup/tick buffer parity.\n";
    } catch (const std::exception& error) {
        std::cerr << "Host check failed: " << error.what() << '\n';
        return 1;
    }
}
