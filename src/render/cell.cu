#include "render/cell.hpp"
#include "render/primitives.hpp"
#include "extra/check.hpp"
#include <cuda_runtime.h>
#include <driver_types.h>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>

// a standard cube, set up as a constant on the GPU
__constant__ DeviceCube::Data DEVICE_CUBE;

__device__ static bool isFaceExposed(CellRef &c, DeviceWorldView dwv, Direction d){
  const worldcoord adjacent = c.coords + worldcoord(DIRECTION_LUT[d]);

  // Anything outside the visible world counts as exposed, without reading the padding.
  return adjacent.x < 0 || adjacent.y < 0 || adjacent.z < 0 ||
          adjacent.x >= dwv.visibleN() ||
          adjacent.y >= dwv.visibleM() ||
          adjacent.z >= dwv.visibleK() ||
          dwv.readCell(adjacent).elem == Element::AIR;
}

// A face is renderable when its cell is not AIR and its neighbor is AIR.
__device__ static bool isRenderable(CellRef &c, DeviceWorldView dwv, Direction d){
  return c.cell.elem != Element::AIR && isFaceExposed(c, dwv, d);
}

// Bit i is set when the face in Direction i is renderable (X_PLUS = bit 0, ..., Z_MINUS = bit 5).
__device__ uint8_t getFaceBitmask(CellRef &c, DeviceWorldView dwv){
  uint8_t face_bitmask = 0x00;
  for(uint8_t i=0; i<6; i++){
    face_bitmask |= ((isRenderable(c, dwv, static_cast<Direction>(i))?1:0) << i);
  }

  return face_bitmask;
}

const DeviceCube &DeviceCube::instance(){
  static const DeviceCube cube;
  return cube;
}

// Geometry pointers come from HostCube's Geometry; the whole struct is copied into
// the __constant__ DEVICE_CUBE that device code reads.
DeviceCube::DeviceCube(){
  HostCube &host = HostCube::instance();

  host.CUBE_GEOM.transfer(); // the cube must be on the GPU before its handle is copied below
  const DeviceGeometryData &cube_gd = *host.CUBE_GEOM.device();
  verts = cube_gd.verts;
  tris = cube_gd.tris;
  vertexCount = cube_gd.vertexCount;
  triangleCount = cube_gd.triangleCount;

  const Data &data = *this;
  CUDA_CHECK(cudaMemcpyToSymbol(DEVICE_CUBE, &data, sizeof(Data)));
}

// Direction of a triangle's outward face: the dominant axis of its normal, with the sign picking +/-.
// Assumes axis-aligned triangles, as the cube's are.
// TODO: we call into this multiple times from different kernels; maybe consolidate & compute once, and save to a Tri
// so that we don't recompute? would involve making Tri a struct of Direction and three indices
MEEP_DEVICE Direction triangleDirection(const Vertex &a, const Vertex &b, const Vertex &c){
  const glm::dvec3 n = glm::cross(glm::dvec3(b) - glm::dvec3(a), glm::dvec3(c) - glm::dvec3(a));

  int axis = 0;
  if(glm::abs(n.y) > glm::abs(n[axis])) axis = 1;
  if(glm::abs(n.z) > glm::abs(n[axis])) axis = 2;

  // Direction values: X_PLUS=0, X_MINUS=1, Y_PLUS=2, Y_MINUS=3, Z_PLUS=4, Z_MINUS=5
  return static_cast<Direction>(2*axis + (n[axis] < 0 ? 1 : 0));
}

// Device only. Sets triMask so each triangle is shown iff the face in its outward direction is shown.
MEEP_DEVICE void CellRenderEntity::setFaceMask(uint8_t face_bits){
  for(std::size_t i=0; i<geom->triangleCount; i++){
    const Tri t = geom->tris[i];
    const Direction d = triangleDirection(geom->verts[t.x], geom->verts[t.y], geom->verts[t.z]);

    triMask[i] = (face_bits >> static_cast<uint8_t>(d)) & 1u;
  }
}
