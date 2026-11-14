#include "extra/cuda_raii.hpp"
#include "render/cell.hpp"
#include "render/render.hpp"
#include "state/state.hpp"
#include "render/camera.hpp"
#include <glm/fwd.hpp>

void worldToClipLaunch(){
  // Transform every vertex in the world by Camera.getVP
  // - To do this, we have CellRenderEntities, each with a transform on the same base cube object. Those transforms are really only translations
  // at this stage; the camera's projection matrix and view matrix should compose with it to come up with a separate transform per 
  // CellRenderEntity in some DeviceBuffer
 
  Renderer &r = Renderer::instance();
  std::size_t num_entities = r.entities->size();

  Camera &c = Camera::instance();
  glm::dmat4x4 vp = c.getVP();
  
  uint16_t threads_per_block = 256;
  std::size_t blocks = (num_entities + threads_per_block - 1) / threads_per_block;
  
  // 6 faces per entity, 2 triangles per face
  // Allocate 2 times needed triangles to overspec for the clipping pass (worst-case clipping causes 1 triangle to turn into 2)
  // tris->size() = 2 * (num_entities * 6 faces * 2 tris per face)
  
  // 8 verts per entity
  // Allocate 1 extra vert per vert to account for creating new vert in the case of clipping
  // verts->size() = 2 * (num_entities * 8 verts)
  r.tris = std::make_unique<DeviceBuffer<Tri>>(2 * (num_entities * HostCube::numTris()));
  CUDA_CHECK(cudaMemset(r.tris->data(), 0xFF, 2 * (num_entities * HostCube::numTris()) * sizeof(Tri)));
  r.verts = std::make_unique<DeviceBuffer<Vertex>>(2 * (num_entities * HostCube::numVerts()));

  

  generateTris<<<blocks, threads_per_block>>>(r.entities->data(), r.verts->data(), r.tris->data(), num_entities, vp);
  cudaDeviceSynchronize();

#if (MEEP_DEBUG == 1)
  std::vector<Vertex> host_verts(2 * (num_entities * HostCube::numVerts()));
  CUDA_CHECK(cudaMemcpy(host_verts.data(), r.verts->data(), 2 * (num_entities * HostCube::numVerts()) * sizeof(Vertex), cudaMemcpyDeviceToHost));

  printf("verts: ");
  for(std::size_t i=0; i<2 * (num_entities * HostCube::numVerts()); i++){
    printf("%f %f %f %f \n", host_verts[i].x, host_verts[i].y, host_verts[i].z, host_verts[i].w);
  }

  std::vector<Tri> host_tris(num_entities * HostCube::numTris());
  CUDA_CHECK(cudaMemcpy(host_tris.data(), r.tris->data(), host_tris.size() * sizeof(Tri), cudaMemcpyDeviceToHost));

  printf("tris: ");
  for(std::size_t i=0; i<host_tris.size(); i++){
    printf("%u %u %u \n", host_tris[i].x, host_tris[i].y, host_tris[i].z);
  }
#endif
}

__global__ void generateTris(const CellRenderEntity *entities, Vertex *verts, Tri *tris, std::size_t num_entities, glm::dmat4x4 vp){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= num_entities) return;

  const CellRenderEntity &ent = entities[ind];

  const DeviceGeometryData &g = *ent.geom;
  std::size_t vert_ct = g.vertexCount;
  std::size_t tri_ct= g.triangleCount;

  glm::dmat4x4 t = vp * ent.transform;

  for(uint8_t i=0; i<vert_ct; i++){
    verts[ind*vert_ct*2 + i] = t * g.verts[i]; // fill in the actual 8 verts
    // we leave 8 spots empty in case we need to create more verts for clipping purposes
  }

  uint32_t ofs = ind*vert_ct*2;
  for (uint8_t i = 0; i < tri_ct; i++) {
    if (ent.triMask[i]) {
        tris[ind * tri_ct * 2 + i] = g.tris[i] + glm::uvec3(ofs, ofs, ofs);
    }
}
}

