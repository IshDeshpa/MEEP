#include "extra/cuda_raii.hpp"
#include "render/cell.hpp"
#include "render/primitives.hpp"
#include "render/render.hpp"
#include "state/state.hpp"
#include <cuda_runtime_api.h>
#include <cstdint>
#include <cstdio>
#include <vector>

void stateToWorldLaunch(WorldState &ws){
  static Renderer &r = Renderer::instance();

  // For each cell in the WorldState:
  // - Is any face visible? (1 or 0)
  // - Record in byte array
  // Second pass over all cells in WorldState
  // - Scan byte array on GPU to figure out index, store in second index array
  // Third pass over all cells in WorldState
  // - If i'm visible, create a new CellRef at the index in the CellRef array
  // - Then, create a new CellRenderEntity for every visible CellRef
  // - We've just culled all cells to only those which are exposed! Yippee!

  DeviceWorldView snapshot = ws.device_view;
  std::size_t world_size = snapshot.size();

  DeviceBuffer<uint8_t> is_visible(world_size);
  DeviceBuffer<uint32_t> offsets(world_size);

  uint16_t threads_per_block = 256;
  std::size_t blocks = (world_size + threads_per_block - 1) / threads_per_block;
  isVisible<<<blocks, threads_per_block>>>(ws.device_view, is_visible.data());

#if (MEEP_DEBUG == 1)
  cudaDeviceSynchronize();
  std::vector<uint8_t> visible_host(world_size);
  CUDA_CHECK(cudaMemcpy(visible_host.data(), is_visible.data(), world_size * sizeof(uint8_t), cudaMemcpyDeviceToHost));

  printf("is_visible: ");
  for(std::size_t i=0; i<world_size; i++){
    printf("%u ", static_cast<unsigned>(visible_host[i]));
  }
#endif

  unsigned num_refs = scanByteNaiveLaunch(is_visible, offsets, blocks, threads_per_block);

#if (MEEP_DEBUG == 1)
  cudaDeviceSynchronize();
  std::vector<uint32_t> offsets_host(world_size);
  CUDA_CHECK(cudaMemcpy(offsets_host.data(), offsets.data(), world_size * sizeof(uint32_t), cudaMemcpyDeviceToHost));

  printf("\noffsets: ");
  for(std::size_t i=0; i<world_size; i++){
    printf("%u ", static_cast<unsigned>(offsets_host[i]));
  }
#endif

  r.entities = std::make_unique<DeviceBuffer<CellRenderEntity>>(num_refs);
  createRefsEntities<<<blocks, threads_per_block>>>(snapshot, is_visible.data(), offsets.data(), r.entities->data(), world_size);

  cudaDeviceSynchronize();

#if (MEEP_DEBUG == 1)
  std::vector<CellRenderEntity> cre_host(num_refs);
  CUDA_CHECK(cudaMemcpy(cre_host.data(), r.entities->data(), num_refs * sizeof(CellRenderEntity), cudaMemcpyDeviceToHost));

  printf("\ncell_render_entities: ");
  for(std::size_t i=0; i<num_refs; i++){
    cre_host[i].print();
  }

#endif

  // all DeviceBuffers will free themselves here
}

__global__ void isVisible(DeviceWorldView dwv, uint8_t *is_visible){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= dwv.size()) return;

  worldcoord coords = dwv.coord(ind);
  Cell cell = dwv.readCell(coords);
  CellRef cr{cell, coords};

  is_visible[ind] = isAnyFaceRenderable(cr, dwv);
}

// Create render cell references & entities (basically; a copy of all cells we may need to render)
__global__ void createRefsEntities(DeviceWorldView dwv, uint8_t *is_visible, uint32_t *offsets, CellRenderEntity *ent_out, std::size_t world_sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= world_sz) return;

  if(is_visible[ind] != 1) return;

  worldcoord c = dwv.coord(ind);
  Cell cell = dwv.readCell(c);
  ent_out[offsets[ind]] = CellRenderEntity(
    cell, c,
    &DEVICE_CUBE,
    ELEMENT_COLOR_LUT[cell.elem]
  );
}
