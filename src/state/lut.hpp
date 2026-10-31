#pragma once

#include "extra/cuda_compat.hpp"
#include "config.hpp"

#include <cstdint>

// Four material IDs, ordered top-left, top-right, bottom-left, bottom-right.
using FaceETile = std::uint16_t;
#define FACE_LUT_ELEMS (UINT16_MAX + 1)
#define FACE_LUT_SIZE (UINT16_MAX + 1) * sizeof(FaceETile)

constexpr FaceETile cell0 = 0xF000;
constexpr FaceETile cell1 = 0x0F00;
constexpr FaceETile cell2 = 0x00F0;
constexpr FaceETile cell3 = 0x000F;

struct CubeTile;
struct DeviceLUTView;

struct LUT {
#if FACE_TRANSITION
    FaceETile *d_lut;

    LUT(const char* f);
    ~LUT();

    DeviceLUTView deviceView() const;
#endif
};

struct DeviceLUTView {
#if FACE_TRANSITION
    const FaceETile* lut;

    MEEP_HOST_DEVICE FaceETile lookup(FaceETile index);
#endif
};

static_assert(sizeof(FaceETile) == 2);

MEEP_HOST_DEVICE
FaceETile packFace(const CubeTile& tile, unsigned tl, unsigned tr,
                   unsigned bl, unsigned br);

// Replace only material IDs; reserved state bits remain unchanged.
MEEP_HOST_DEVICE
void unpackFace(FaceETile face, CubeTile& tile, unsigned tl, unsigned tr,
                 unsigned bl, unsigned br);

