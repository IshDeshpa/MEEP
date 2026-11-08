#include "render/cell.hpp"
#include "render/primitives.hpp"
#include "extra/check.hpp"
#include <cuda_runtime.h>
#include <driver_types.h>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>

// a standard cube, set up as a constant on the GPU
__constant__ DeviceCube::Data DEVICE_CUBE;

__device__ static bool isFaceExposed(CellRef &c, DeviceWorldView dwv, const CubeFace &face){
  const worldcoord adjacent = c.coords + worldcoord(DIRECTION_LUT[face.d]);

  // Anything outside the visible world counts as exposed, without reading the padding.
  return adjacent.x < 0 || adjacent.y < 0 || adjacent.z < 0 ||
          adjacent.x >= dwv.visibleN() ||
          adjacent.y >= dwv.visibleM() ||
          adjacent.z >= dwv.visibleK() ||
          dwv.readCell(adjacent).elem == Element::AIR;
}

// A face is renderable when its cell is not AIR and its neighbor is AIR.
__device__ static bool isRenderable(CellRef &c, DeviceWorldView dwv, const CubeFace &face){
  return c.cell.elem != Element::AIR && isFaceExposed(c, dwv, face);
}

__device__ uint8_t getFaceBitmask(CellRef &c, DeviceWorldView dwv){
  uint8_t face_bitmask = 0x00;
  for(int i=0; i<DEVICE_CUBE.triangleCount/2; i++){
    const CubeFace &face = DEVICE_CUBE.faces[i];
    face_bitmask |= ((isRenderable(c, dwv, face)?1:0) << static_cast<uint8_t>(face.d));
  }

  return face_bitmask;
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
  const DeviceGeometryData &cube_gd = *host.CUBE_GEOM.device();
  verts = cube_gd.verts;
  tris = cube_gd.tris;
  vertexCount = cube_gd.vertexCount;
  triangleCount = cube_gd.triangleCount;
  faces = d_faces;

  const Data &data = *this;
  CUDA_CHECK(cudaMemcpyToSymbol(DEVICE_CUBE, &data, sizeof(Data)));
}


// Device only. Walks the cube's faces: each face records which two triangles belong to it and its Direction,
// so the mask is built from the face data rather than assuming triangle indices.
MEEP_DEVICE void CellRenderEntity::setFaceMask(uint8_t face_bits){
  for(int i=0; i<6; i++){
    const CubeFace &face = DEVICE_CUBE.faces[i];
    const bool shown = (face_bits >> static_cast<uint8_t>(face.d)) & 1u;
    if(face.a < geom->triangleCount) triMask[face.a] = shown;
    if(face.b < geom->triangleCount) triMask[face.b] = shown;
  }
}
