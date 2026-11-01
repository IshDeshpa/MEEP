#pragma once

#include <glm/glm.hpp>
#include <limits>

#include <stddef.h>
#include <array>

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

  static constexpr const char *CUBE_PATH = "./assets/cube.obj";
  Geometry CUBE_GEOM{CUBE_PATH}; // CPU copy; DeviceCube transfers it to the GPU
  const std::array<CubeFace, 6> CUBE_FACES{makeFaces()}; // 6 faces for a cube

  static HostCube &instance();

  HostCube(const HostCube &) = delete;
  HostCube &operator=(const HostCube &) = delete;

private:
  HostCube() = default;
};

struct DeviceCubeData : public DeviceGeometryData { // Faces also reside in GPU memory.
  const CubeFace *faces = nullptr;
};

// We need the class (with the functions) to be separate from the GPU resident data struct,
// because otherwise we can't instantiate a const DeviceCube because it'll call the constructor
class DeviceCube : public DeviceCubeData {
public:
  using Data = DeviceCubeData;

  // CPU singleton, built on first use. Not a namespace-scope static: its constructor
  // calls CUDA, and that runs before the CUDA runtime has registered this module.
  static const DeviceCube &instance();

  DeviceCube(const DeviceCube &) = delete;
  DeviceCube &operator=(const DeviceCube &) = delete;

private:
  DeviceCube();
};

class CellRenderEntity : public DeviceEntity {
  CellRef &cell_ref;

  MEEP_DEVICE static CellRenderEntity *create(CellRef &c, DeviceWorldView dwv);
  MEEP_DEVICE CellRenderEntity(CellRef &cr);
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
