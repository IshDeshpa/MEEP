#pragma once

#include <glm/glm.hpp>
#include <limits>
using namespace glm;

#include <stddef.h>
#include <memory>
#include <array>
#include <vector>

#include "render/primitives.hpp"
#include "state/state.hpp"

// A cell reference wrapper that also has
// self-contained coords
struct CellRef{
  Cell &cell;
  coord coords;
};

struct CellFace{
  TriRef a = std::numeric_limits<unsigned int>().max();
  TriRef b = std::numeric_limits<unsigned int>().max();
  Direction d; // what side of the cube is it on?
};

class CellRenderEntity : public Entity{
private:
  static const std::array<CellFace, 6> makeFaces();

public:
  CellRef &cell_ref;

  inline static const Geometry CUBE_GEOM{"./assets/cube.obj"}; // buffers
  inline static const Mesh CUBE_MESH{CUBE_GEOM, 0, static_cast<TriRef>(CUBE_GEOM.size()-1)}; // spots in buffers
  inline static std::array<CellFace, 6> CUBE_FACES{makeFaces()}; // 6 faces for a cube

  CellRenderEntity(CellRef &cr): Entity(CUBE_MESH, lutGet(ELEMENT_COLOR_LUT, cr.cell.elem), cr.coords, dvec3(0, 0, 0)), cell_ref(cr){}

  unsigned int updateMeshMask(WorldState &ws); // return new count of visible faces
  void updateColor(); // based on cell reference's current elem

  static std::unique_ptr<CellRenderEntity> create(CellRef &c, WorldState &ws); // factory function, call with CellRenderEntity.create

};

