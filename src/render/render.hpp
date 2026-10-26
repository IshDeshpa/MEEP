#pragma once

#define GLM_FORCE_CUDA
#include <glm/glm.hpp>

#include <state/state.hpp>
#include <stddef.h>

using namespace glm;

// CUDA functions to expose
void helloWorld(); // to test

using Vertex=dvec3;
using Triangle=uvec3;

struct Color {
  uint8_t r; uint8_t g; uint8_t b;
};

enum class Direction : uint8_t{
  X_PLUS=0,
  X_MINUS,
  Y_PLUS,
  Y_MINUS,
  Z_PLUS,
  Z_MINUS
};

struct Face{
  Triangle a; // TL, BR, BL
  Triangle b; // TR, BR, TL
};

class CellRenderEntity{
  Cell &cell;
  Color color;
  std::vector<Face> rendered_faces;

  CellRenderEntity(Cell &cell): cell(cell){
    
  }

};

class Pose {
  dvec4 pos = dvec4(0, 0, 0, 1); // position
  dvec4 look = dvec4(0, 0, -1, 0); // look direction

  Pose() = default;
  Pose(const dvec4 &p, const dvec4 &l): pos(p), look(l) {}
};

