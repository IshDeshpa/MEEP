#pragma once

#include <glm/glm.hpp>
#include <limits>

#include <stddef.h>
#include <array>

#include "extra/cuda_compat.hpp"
#include "render/primitives.hpp"
#include "state/state.hpp"

// A cell that also has self-contained coords
struct CellRef{
  Cell cell;
  worldcoord coords;

  MEEP_HOST_DEVICE CellRef(Cell cell, worldcoord c): cell(cell), coords(c){}
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

#ifdef __CUDACC__
extern __constant__ DeviceCube::Data DEVICE_CUBE;
#endif

class CellRenderEntity : public DeviceEntity {
  CellRef cell_ref;
  
public:
  // Placeholder for containers that default-construct; overwritten by a copy from the GPU
  CellRenderEntity(): DeviceEntity(), cell_ref(Cell{}, worldcoord(0)) {}

  MEEP_HOST_DEVICE CellRenderEntity(Cell c, worldcoord coord, const DeviceCubeData *dcd, Color color): DeviceEntity(dcd, color, coord, glm::dvec3(0.0)), cell_ref(CellRef(c, coord)) {}
  MEEP_HOST_DEVICE CellRenderEntity(CellRef cr, const DeviceCubeData *dcd, Color color): DeviceEntity(dcd, color, cr.coords, glm::dvec3(0.0)), cell_ref(cr) {}

  // Host only: prints the cell, its coords and its color to stdout
  void print() const;
};

MEEP_DEVICE bool isAnyFaceRenderable(CellRef &c, DeviceWorldView dwv);
