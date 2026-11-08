#include "render/render.hpp"
#include <limits>

void clipToNDCLaunch(){
  Renderer &r = Renderer::instance();

  uint16_t threads_per_block = 256;
  std::size_t verts_sz = r.verts->size();
  std::size_t tris_sz = r.tris->size();
  std::size_t blocks = (verts_sz + threads_per_block - 1) / threads_per_block;

  clipToNDC<<<blocks, threads_per_block>>>(r.verts->data(), verts_sz);
  cudaDeviceSynchronize();

  blocks = (r.tris->size() + threads_per_block - 1) / threads_per_block;
  backfaceCull<<<blocks, threads_per_block>>>(r.verts->data(), r.tris->data(), tris_sz);
  cudaDeviceSynchronize();
}

// Modifies in place!
__global__ void clipToNDC(Vertex *verts, std::size_t verts_sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= verts_sz) return;

  Vertex::value_type max = std::numeric_limits<Vertex::value_type>().max();
  if(verts[ind].w == max) return; // if w is already max it's cooked and we gotta skip this vert
  if(verts[ind].w <= 0) verts[ind] = Vertex(max, max, max, max); // this should not be true ever if we've culled

  verts[ind] = verts[ind]/verts[ind].w;
}

// Backface culling
__global__ void backfaceCull(const Vertex *verts, Tri *tris, std::size_t tris_sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= tris_sz) return;

  Vertex::value_type max_vert = std::numeric_limits<Vertex::value_type>().max();
  Tri::value_type max_tri = std::numeric_limits<Tri::value_type>().max();

  if(tris[ind].x == max_tri) return;

  Vertex a = verts[tris[ind].x]; if(a.w == max_vert) return;
  Vertex b = verts[tris[ind].y]; if(b.w == max_vert) return;
  Vertex c = verts[tris[ind].z]; if(c.w == max_vert) return;

  int area_2d = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
  if(area_2d <= 0){
    // Cull
    tris[ind].x = max_tri;
    tris[ind].y = max_tri;
    tris[ind].z = max_tri;
  }
}
