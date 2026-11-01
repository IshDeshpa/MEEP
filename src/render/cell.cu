#include "render/cell.hpp"
#include "render/primitives.hpp"
#include "extra/check.hpp"
#include <cuda_runtime.h>
#include <driver_types.h>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>

// a standard cube, set up as a constant on the GPU
__constant__ DeviceCube::Data DEVICE_CUBE;

__device__ static inline bool isFaceExposed(CellRef &c, DeviceWorldView dwv, const CubeFace &face){
  const worldcoord adjacent = c.coords + worldcoord(DIRECTION_LUT[face.d]);

  return adjacent.x < 0 || adjacent.y < 0 || adjacent.z < 0 ||
          adjacent.x >= (dwv.n_pad) ||
          adjacent.y >= (dwv.m_pad) ||
          adjacent.z >= (dwv.k_pad) || 
          dwv.readCell(adjacent).elem == Element::AIR;
}

// A face is renderable when its cell is not AIR and its neighbor is AIR.
__device__ static inline bool isRenderable(CellRef &c, DeviceWorldView dwv, const CubeFace &face){
  return c.cell.elem != Element::AIR && isFaceExposed(c, dwv, face);
}

__device__ static inline bool isAnyFaceRenderable(CellRef &c, DeviceWorldView dwv){
  bool res = false;
  for(int i=0; i<DEVICE_CUBE.triangleCount/2; i++){
    const CubeFace &face = DEVICE_CUBE.faces[i];
    res = res || isRenderable(c, dwv, face);
  }

  return res;
}

const DeviceCube &DeviceCube::instance(){
  static const DeviceCube cube;
  return cube;
}

// Geometry pointers come from HostCube's Geometry, faces are uploaded here,
// and the whole struct is copied into the __constant__ DEVICE_CUBE that device code reads.
DeviceCube::DeviceCube(){
  HostCube &host = HostCube::instance();

  const size_t faces_size = host.CUBE_FACES.size() * sizeof(CubeFace);
  CubeFace *d_faces = nullptr;
  CUDA_CHECK(cudaMalloc(&d_faces, faces_size));
  CUDA_CHECK(cudaMemcpy(d_faces, host.CUBE_FACES.data(), faces_size, cudaMemcpyHostToDevice));

  host.CUBE_GEOM.transfer(); // the cube must be on the GPU before its handle is copied below
  const DeviceGeometryData &cube_gd = host.CUBE_GEOM.device();
  verts = cube_gd.verts;
  tris = cube_gd.tris;
  vertexCount = cube_gd.vertexCount;
  triangleCount = cube_gd.triangleCount;
  faces = d_faces;

  const Data &data = *this;
  CUDA_CHECK(cudaMemcpyToSymbol(DEVICE_CUBE, &data, sizeof(Data)));
}

// Generate CellRenderEntity only for those cells that could possibly
// be rendered (track state of any non-obstructed cell)
__device__ CellRenderEntity *CellRenderEntity::create(CellRef &c, DeviceWorldView dwv){
  return (!isAnyFaceRenderable(c, dwv)) ? nullptr : new CellRenderEntity(c); // consider non-renderable cells as obstructed
}

__device__ CellRenderEntity::CellRenderEntity(CellRef &cr)
    : DeviceEntity(DEVICE_CUBE, ELEMENT_COLOR_LUT[cr.cell.elem], cr.coords,
                   glm::dvec3(0, 0, 0)),
      cell_ref(cr) {
}

