#include "extra/cuda_raii.hpp"
#include "render/cell.hpp"
#include "render/render.hpp"
#include "state/state.hpp"
#include "render/camera.hpp"
#include <glm/fwd.hpp>

void worldToClipLaunch(WorldState &ws){
  // Transform every vertex in the world by Camera.getVP
  // - To do this, we have CellRenderEntities, each with a transform on the same base cube object. Those transforms are really only translations
  // at this stage; the camera's projection matrix and view matrix should compose with it to come up with a separate transform per 
  // CellRenderEntity in some DeviceBuffer
 
  Renderer &r = Renderer::instance();

  Camera &c = Camera::instance(); 
  glm::dmat4x4 vp = c.syncPose(); // update the GPU pose 
  
  uint16_t threads_per_block = 256;
  std::size_t blocks = (r.entities->size() + threads_per_block - 1) / threads_per_block;

  DeviceBuffer<glm::dmat4x4> transforms(r.entities->size());
  generateTransforms<<<blocks, threads_per_block>>>(r.entities->data(), r.entities->size(), transforms.data(), vp);

  // We have the transforms of every object; now, we operate on each vertex and decide if it's clipped
  // 8 vertices per entity
}

__global__ void generateTransforms(const CellRenderEntity *entities, std::size_t sz, glm::dmat4x4 *transforms, glm::dmat4x4 vp){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= sz) return;

  transforms[ind] = vp * entities[ind].transform;
}

