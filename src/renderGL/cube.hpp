#pragma once

#include <cstdint>
#include <vector>
#include "state/state.hpp"

namespace Cube {
// Eight shared xyz vertices; two triangles per face, in Direction order.
extern const std::vector<float> vertices;
extern const std::vector<std::uint32_t> indices;
}

// Bits follow Direction order; a set bit denotes an exposed face.
MEEP_DEVICE uint8_t getFaceMask(DeviceWorldView dwv, Coord coords, Cell cell);
