#pragma once

#include "../thread_count/benchmark_common.h"
#include "../fsm_lookup_test/fsm_helpers.h"

#include <array>
#include <type_traits>

#ifdef __CUDACC__
#define TV_HD __host__ __device__
#define TV_UNROLL _Pragma("unroll")
#else
#define TV_HD
#define TV_UNROLL
#endif

namespace tv {

enum class Shape { cube, face, edge };

template <Shape Kind>
struct Traits {
    static constexpr unsigned cells = Kind == Shape::cube ? 8 : Kind == Shape::face ? 4 : 2;
    static constexpr unsigned lookups = Kind == Shape::cube ? 1 : Kind == Shape::face ? 6 : 12;
    using Entry = std::conditional_t<Kind == Shape::cube, uint32_t,
                  std::conditional_t<Kind == Shape::face, uint16_t, uint8_t>>;
};

inline const char* shape_name(Shape kind) {
    return kind == Shape::cube ? "cube" : kind == Shape::face ? "face" : "edge";
}

template <Shape Kind, bool Sequential>
constexpr const char* mode_name() {
    return Kind == Shape::cube ? "single" : Sequential ? "sequential" : "independent";
}

inline meep::StateSpace state_space(unsigned materials, unsigned cells) {
    meep::state_count(materials);  // Validate the common 2..16 material range.
    if (cells != 2 && cells != 4 && cells != 8)
        throw std::invalid_argument("a transition must contain 2, 4, or 8 cells");
    uint64_t count = 1, enclosing = 1;
    for (unsigned cell = 0; cell < cells; ++cell) count *= materials;
    while (enclosing < count) enclosing *= 2;
    return {count, static_cast<uint32_t>(enclosing - 1)};
}

template <Shape Kind>
TV_HD inline unsigned part_cell(unsigned part, unsigned position) {
    if constexpr (Kind == Shape::face) return face_cell(part, position);
    else return edge_cell(part, position);
}

// Worlds and tables use dense base-materials codes, including nonpowers of two.
// Decode the world load once; all proposals and overwrites stay thread-local.
template <Shape Kind, bool Sequential, class Lookup>
TV_HD inline uint32_t update_tile(uint32_t input, unsigned materials, Lookup lookup) {
    if constexpr (Kind == Shape::cube) {
        return lookup(input);
    } else {
        uint32_t original[8], temporary[8];
        TV_UNROLL
        for (unsigned cell = 0; cell < 8; ++cell) {
            original[cell] = input % materials;
            temporary[cell] = original[cell];
            input /= materials;
        }
        // Fixed order: x faces, y faces, z faces; x edges, y edges, z edges.
        // Independent proposals read original[], while sequential proposals
        // read temporary[]. Both write temporary[] with later parts winning.
        TV_UNROLL
        for (unsigned part = 0; part < Traits<Kind>::lookups; ++part) {
            uint32_t key = 0;
            TV_UNROLL
            for (unsigned p = Traits<Kind>::cells; p-- > 0;) {
                const unsigned cell = part_cell<Kind>(part, p);
                key = key * materials + (Sequential ? temporary[cell] : original[cell]);
            }
            uint32_t result = lookup(key);
            TV_UNROLL
            for (unsigned p = 0; p < Traits<Kind>::cells; ++p) {
                temporary[part_cell<Kind>(part, p)] = result % materials;
                result /= materials;
            }
        }
        uint32_t result = 0;
        TV_UNROLL
        for (unsigned cell = 8; cell-- > 0;) result = result * materials + temporary[cell];
        return result;
    }
}

// Independent table loads must remain observable even when a later part
// overwrites every voxel they propose. Otherwise dead-code elimination can
// reduce six/twelve lookups to two/four. Sequential/cube loads are ordinary.
template <class Entry, bool Independent>
struct TableLookup {
    const Entry* table;
    TV_HD uint32_t operator()(uint32_t key) const {
        if constexpr (Independent) return static_cast<const volatile Entry*>(table)[key];
        else return table[key];
    }
};

// A separate CPU implementation uses explicit cell arrays and an on-demand
// synthetic transition. It neither allocates a cube LUT nor uses update_tile.
// Geometry is stated explicitly to catch extraction/order mistakes in kernels.
inline uint32_t reference_update(uint32_t input, unsigned materials, Shape kind, bool sequential) {
    if (kind == Shape::cube) return meep::transition(input, meep::state_space(materials));
    static constexpr unsigned faces[6][4] = {
        {0, 2, 4, 6}, {1, 3, 5, 7}, {0, 1, 4, 5},
        {2, 3, 6, 7}, {0, 1, 2, 3}, {4, 5, 6, 7}
    };
    static constexpr unsigned edges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7},
        {0, 2}, {1, 3}, {4, 6}, {5, 7},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    std::array<uint32_t, 8> original{}, temporary{};
    for (auto& cell : original) { cell = input % materials; input /= materials; }
    temporary = original;
    const unsigned cells = kind == Shape::face ? 4 : 2;
    const unsigned parts = kind == Shape::face ? 6 : 12;
    const auto space = state_space(materials, cells);
    for (unsigned part = 0; part < parts; ++part) {
        uint32_t key = 0, place = 1;
        for (unsigned p = 0; p < cells; ++p) {
            const unsigned cell = kind == Shape::face ? faces[part][p] : edges[part][p];
            key += (sequential ? temporary[cell] : original[cell]) * place;
            place *= materials;
        }
        uint32_t result = meep::transition(key, space);
        for (unsigned p = 0; p < cells; ++p) {
            const unsigned cell = kind == Shape::face ? faces[part][p] : edges[part][p];
            temporary[cell] = result % materials;
            result /= materials;
        }
    }
    uint64_t result = 0, place = 1;
    for (uint32_t cell : temporary) { result += cell * place; place *= materials; }
    return static_cast<uint32_t>(result);
}

}  // namespace tv

#undef TV_HD
#undef TV_UNROLL
