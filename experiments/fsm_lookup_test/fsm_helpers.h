#ifndef FSM_HELPERS_H
#define FSM_HELPERS_H
#include <cstdint>
#ifdef __CUDACC__
#define FSM_HD __host__ __device__
#else
#define FSM_HD
#endif

// Cube cell numbering: cell = x + 2*y + 4*z; each cell is one nibble.
FSM_HD inline std::uint32_t mix32(std::uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
FSM_HD inline unsigned face_cell(unsigned face, unsigned position)
{
    const unsigned a = position & 1u, b = position >> 1;
    if (face < 2) return face + 2u * a + 4u * b; // x fixed
    if (face < 4) return a + 2u * (face - 2u) + 4u * b; // y fixed
    return a + 2u * b + 4u * (face - 4u); // z fixed
}
FSM_HD inline unsigned edge_cell(unsigned edge, unsigned position)
{
    const unsigned group = edge / 4u, k = edge & 3u;
    if (group == 0) return position + 2u * (k & 1u) + 4u * (k >> 1);
    if (group == 1) return (k & 1u) + 2u * position + 4u * (k >> 1);
    return (k & 1u) + 2u * (k >> 1) + 4u * position;
}
FSM_HD inline unsigned cell_for(int variant, unsigned part, unsigned position)
{
    return variant == 1 ? face_cell(part, position) : edge_cell(part, position);
}
FSM_HD inline std::uint32_t extract_subtile(std::uint32_t cube, int variant, unsigned part)
{
    std::uint32_t index = 0;
    const unsigned cells = variant == 1 ? 4u : 2u;
    for (unsigned p = 0; p < cells; ++p)
        index |= ((cube >> (4u * cell_for(variant, part, p))) & 15u) << (4u * p);
    return index;
}
FSM_HD inline std::uint32_t replace_subtile(std::uint32_t cube, int variant,
                                          unsigned part, std::uint32_t result)
{
    const unsigned cells = variant == 1 ? 4u : 2u;
    for (unsigned p = 0; p < cells; ++p) {
        const unsigned shift = 4u * cell_for(variant, part, p);
        cube = (cube & ~(15u << shift)) | (((result >> (4u * p)) & 15u) << shift);
    }
    return cube;
}
// Synthetic deterministic transition; NOT a physical reaction model.
FSM_HD inline std::uint32_t transition(std::uint32_t input)
{
    return mix32(input ^ 0xa511e9b3u);
}
FSM_HD inline std::uint32_t input_cube(unsigned thread, unsigned iteration)
{
    return mix32(thread * 747796405u + iteration * 2891336453u + 17u);
}

// CPU reference with on-demand transitions, avoiding a 16-GiB host allocation.
// Variant 0: cube, 1: six faces, 2: twelve edges.
template <int Variant, bool Sequential>
FSM_HD inline std::uint32_t reference_update(std::uint32_t input)
{
    if constexpr (Variant == 0) return transition(input);
    else {
        std::uint32_t state = input, checksum = 0;
        constexpr unsigned parts = Variant == 1 ? 6u : 12u;
        constexpr unsigned mask = Variant == 1 ? 65535u : 255u;
        for (unsigned part = 0; part < parts; ++part) {
            const auto result = transition(extract_subtile(state, Variant, part)) & mask;
            if constexpr (Sequential)
                state = replace_subtile(state, Variant, part, result);
            else
                checksum += result * (part + 1u);
        }
        if constexpr (Sequential) return state;
        else return checksum;
    }
}
#undef FSM_HD
#endif
