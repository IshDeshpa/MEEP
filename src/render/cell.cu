#include "render/cell.hpp"
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>

__device__ static inline bool isFaceExposed(CellRef &c, WorldState &ws, const CubeFace &face){
  const worldcoord adjacent = c.coords + worldcoord(DIRECTION_LUT[face.d]);
  bool res = false;
  if(adjacent.x < 0 || adjacent.y < 0 || adjacent.z < 0 ||
     adjacent.x >= static_cast<int>(ws.n) ||
     adjacent.y >= static_cast<int>(ws.m) ||
     adjacent.z >= static_cast<int>(ws.k)){
    return true; // A world boundary is exposed to the outside.
  }

  return ws(adjacent).elem == Element::AIR;
}

// A face is renderable when its cell is not AIR and its neighbor is AIR.
__device__ static inline bool isRenderable(CellRef &c, WorldState &ws, const CubeFace &face){
  return c.cell.elem != Element::AIR && isFaceExposed(c, ws, face);
}

__device__ static inline bool isAnyFaceRenderable(CellRef &c, WorldState &ws, DeviceCube &dc){
  bool res = false;
  for(int i=0; i<dc.triangleCount; i++){
    const CubeFace &face = dc.faces[i];
    res = res && isRenderable(c, ws, face);
  }

  return res;
}

DeviceCube &DeviceCube::instance(){
  static DeviceCube cube;
  return cube;
}

// Creates a DeviceCube containing GPU pointers; this can be trivially copied
// to the GPU through function parameters
DeviceCube::DeviceCube(){
  const HostCube &host = HostCube::instance();
  
  const size_t verts_size = host.CUBE_GEOM.verts.size() * sizeof(Vertex);
  const size_t tris_size = host.CUBE_GEOM.tris.size() * sizeof(Tri);
  const size_t faces_size = host.CUBE_FACES.size() * sizeof(CubeFace);


  cudaMalloc(&verts, verts_size);
  cudaMemcpy(&verts, host.CUBE_GEOM.verts.data(), verts_size, cudaMemcpyHostToDevice);

  cudaMalloc(&tris, tris_size);
  cudaMemcpy(&tris, host.CUBE_GEOM.tris.data(), tris_size, cudaMemcpyHostToDevice);
  
  cudaMalloc(&faces, faces_size);
  cudaMemcpy(&faces, host.CUBE_GEOM.faces.data(), faces_size, cudaMemcpyHostToDevice);

  triangleCount = host.CUBE_GEOM.size();
}

// Generate CellRenderEntity only for those cells that could possibly
// be rendered (track state of any non-obstructed cell)
__device__ CellRenderEntity *CellRenderEntity::create(CellRef &c, WorldState &ws){
  if(!isAnyFaceRenderable(c, ws)) return nullptr; // consider non-renderable cells as obstructed

  return std::unique_ptr<CellRenderEntity>(new CellRenderEntity(c));
}

