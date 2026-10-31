#pragma once

#include <glm/glm.hpp>
#include <limits>

#include <stddef.h>
#include <memory>
#include <array>
#include <vector>

#include "extra/cuda_compat.hpp"
#include "render/primitives.hpp"
#include "state/state.hpp"

// A cell reference wrapper that also has
// self-contained coords
struct CellRef{
  Cell &cell;
  worldcoord coords;

  CellRef(Cell &cell, worldcoord c): cell(cell), coords(c){}
};

struct CubeFace{
  TriRef a = std::numeric_limits<unsigned int>().max();
  TriRef b = std::numeric_limits<unsigned int>().max();
  Direction d; // what side of the cube is it on?
};

struct HostCube {
  const std::array<CubeFace, 6> makeFaces() const;

  const Geometry CUBE_GEOM{"./assets/cube.obj"}; // buffers
  const std::array<CubeFace, 6> CUBE_FACES{makeFaces()}; // 6 faces for a cube

  static HostCube &instance();

  HostCube(const HostCube &) = delete;
  HostCube &operator=(const HostCube &) = delete;

private:
  HostCube() = default;
};

struct DeviceCube { // Geometry buffers are GPU pointers; faces are inline values.
  const Vertex *verts = nullptr;
  const Tri *tris = nullptr;
  const CubeFace *faces = nullptr;
  std::size_t triangleCount = 0; // triangles occupy [0, triangleCount)
  

  // Call after selecting the CUDA device; initialized on first access.
  static DeviceCube &instance();

  DeviceCube(const DeviceCube &) = delete;
  DeviceCube &operator=(const DeviceCube &) = delete;

private:
  DeviceCube();
};

class CellRenderEntity : public Entity {
  MEEP_DEVICE CellRenderEntity *create(CellRef &c, WorldState &ws);
  
};

// class CellRenderEntity : public Entity{
// private:
//   MEEP_DEVICE static const std::array<CellFace, 6> makeFaces();
//
// public:
//   CellRef &cell_ref;
//
//   inline static const Geometry CUBE_GEOM{"./assets/cube.obj"}; // buffers
//   inline static std::array<CellFace, 6> CUBE_FACES{makeFaces()}; // 6 faces for a cube
//
//   CellRenderEntity(CellRef &cr): Entity(CUBE_GEOM, ELEMENT_COLOR_LUT[cr.cell.elem], cr.coords, glm::dvec3(0, 0, 0)), cell_ref(cr){}
//
//   unsigned int updateTriangleMask(WorldState &ws); // return new count of visible faces
//   void updateColor(); // based on cell reference's current elem
//
//   void print();
//
//   static std::unique_ptr<CellRenderEntity> create(CellRef &c, WorldState &ws); // factory function, call with CellRenderEntity.create
//
// };

//
