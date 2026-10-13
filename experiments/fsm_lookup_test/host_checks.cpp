#include "fsm_helpers.h"
#include <cassert>
#include <iostream>
#include <set>

int main()
{
    std::set<unsigned> face_masks, edge_masks;
    for (unsigned f = 0; f < 6; ++f) {
        unsigned mask = 0;
        for (unsigned p = 0; p < 4; ++p) mask |= 1u << face_cell(f, p);
        assert(__builtin_popcount(mask) == 4);
        face_masks.insert(mask);
    }
    assert(face_masks == std::set<unsigned>({0x55, 0xaa, 0x33, 0xcc, 0x0f, 0xf0}));
    for (unsigned e = 0; e < 12; ++e) {
        const auto a = edge_cell(e, 0), b = edge_cell(e, 1);
        assert(a < 8 && b < 8);
        assert(__builtin_popcount(a ^ b) == 1);
        edge_masks.insert((1u << a) | (1u << b));
    }
    assert(edge_masks.size() == 12);
    for (unsigned seed = 0; seed < 1000; ++seed) {
        const auto cube = mix32(seed);
        assert(extract_subtile(cube, 1, 4) == (cube & 65535u));
        assert(extract_subtile(cube, 1, 5) == (cube >> 16));
        assert(extract_subtile(cube, 2, 0) == (cube & 255u));
        for (int variant = 1; variant <= 2; ++variant) {
            const unsigned parts = variant == 1 ? 6 : 12;
            const unsigned cells = variant == 1 ? 4 : 2;
            const auto result = transition(seed) & (variant == 1 ? 65535u : 255u);
            for (unsigned part = 0; part < parts; ++part) {
                unsigned changed_bits = 0;
                for (unsigned p = 0; p < cells; ++p)
                    changed_bits |= 15u << (4 * cell_for(variant, part, p));
                const auto updated = replace_subtile(cube, variant, part, result);
                assert(extract_subtile(updated, variant, part) == result);
                assert((updated & ~changed_bits) == (cube & ~changed_bits));
            }
        }
    }
    std::cout << "PASS: face/edge geometry and packed cell extraction/replacement\n";
}
