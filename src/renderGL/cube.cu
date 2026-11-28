#include "renderGL/cube.hpp"
#include "state/visual.hpp"

namespace Cube {
// Each face is clockwise when viewed from outside the cube.
const std::vector<float> vertices{
   0.5f, -0.5f, -0.5f,
   0.5f, -0.5f,  0.5f,
   0.5f,  0.5f,  0.5f,
   0.5f,  0.5f, -0.5f,
  -0.5f, -0.5f,  0.5f,
  -0.5f, -0.5f, -0.5f,
  -0.5f,  0.5f, -0.5f,
  -0.5f,  0.5f,  0.5f,
};

const std::vector<std::uint32_t> indices{
   0,  1,  2,   0,  2,  3,  // X_PLUS
   4,  5,  6,   4,  6,  7,  // X_MINUS
   6,  3,  2,   6,  2,  7,  // Y_PLUS
   4,  1,  0,   4,  0,  5,  // Y_MINUS
   1,  4,  7,   1,  7,  2,  // Z_PLUS
   5,  0,  3,   5,  3,  6,  // Z_MINUS
};



} // namespace Cube

__device__ static bool isFaceExposed(DeviceWorldView dwv, Coord coords, Cell cell, Direction d){
  Coord adjacent = coords;
  switch (d) {
    case Direction::X_PLUS:  ++adjacent.x; break;
    case Direction::X_MINUS: --adjacent.x; break;
    case Direction::Y_PLUS:  ++adjacent.y; break;
    case Direction::Y_MINUS: --adjacent.y; break;
    case Direction::Z_PLUS:  ++adjacent.z; break;
    case Direction::Z_MINUS: --adjacent.z; break;
    default: return false;
  }

  if (adjacent.x < 0 || adjacent.y < 0 || adjacent.z < 0 ||
      static_cast<std::size_t>(adjacent.x) >= dwv.visibleN() ||
      static_cast<std::size_t>(adjacent.y) >= dwv.visibleM() ||
      static_cast<std::size_t>(adjacent.z) >= dwv.visibleK()) return true;

  const Cell neighbor = dwv.readCell(adjacent);
  if (neighbor.elem == Element::AIR) return true;
  if (neighbor.elem == cell.elem) return false;
  return ELEMENT_COLOR_LUT[neighbor.elem].a < 255;
}

__device__ static bool isRenderable(DeviceWorldView dwv, Coord coords, Cell cell, Direction d){
  return cell.elem != Element::AIR && isFaceExposed(dwv, coords, cell, d);
}

__device__ uint8_t getFaceMask(DeviceWorldView dwv, Coord coords, Cell cell){
  uint8_t face_bitmask = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(Direction::COUNT); ++i) {
    face_bitmask |= static_cast<uint8_t>(isRenderable(dwv, coords, cell, static_cast<Direction>(i))) << i;
  }
  return face_bitmask;
}
