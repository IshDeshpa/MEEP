#include "extra/check.hpp"
#include "renderGL/renderGL.hpp"
#include "renderGL/cube.hpp"
#include "renderGL/scan.hpp"
#include "renderGL/window.hpp"
#include "state/state.hpp"
#include "extra/device_resources.hpp"
#include <cuda_runtime_api.h>
#include <cstdint>
#include <cstdio>
#include <memory>

__global__ void getFaceMasks(DeviceWorldView dwv, uint8_t *face_masks);
__global__ void fillEntities(DeviceWorldView dwv, const uint8_t *face_masks, const uint32_t *offsets, CellInstance *ent_out, std::size_t world_sz);

bool RendererGL::buildVisibleCells(WorldState &ws){
  // For each cell in the WorldState:
  // - Is any face visible? (1 or 0)
  // - Record in byte array
  // Second pass over all cells in WorldState
  // - Scan byte array on GPU to figure out index, store in second index array
  // Third pass over all cells in WorldState
  // - If i'm visible, create a new RenderEntityGL 
  // - We've just culled all cells to only those which are exposed! Yippee!

  DeviceWorldView snapshot = ws.device_view;
  std::size_t world_size = snapshot.size();
  
  DeviceBuffer<uint8_t> face_masks(world_size);
  DeviceBuffer<uint32_t> offsets(world_size);

  uint16_t threads_per_block = 256;
  std::size_t blocks = (world_size + threads_per_block - 1) / threads_per_block;
  getFaceMasks<<<blocks, threads_per_block, 0, this->render_stream>>>(snapshot, face_masks.data());
  KERNEL_CHECK();

  unsigned num_refs = scanByteNaiveLaunch(face_masks, offsets, blocks, threads_per_block);
  if(num_refs == 0){
    return false;
  }

  if(num_refs > this->capacity)

  return true;
}

__global__ void getFaceMasks(DeviceWorldView dwv, uint8_t *face_masks){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= dwv.size()) return;

  Coord coords = dwv.coord(ind);
  Cell cell = dwv.readCell(coords);

  face_masks[ind] = getFaceMask(dwv, coords, cell);
}

__global__ void fillEntities(DeviceWorldView dwv, const uint8_t *face_masks, const uint32_t *offsets, CellInstance *ent_out, std::size_t world_sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= world_sz) return;
   
  if(face_masks[ind] == 0) return; // completely obscured, non-exposed

  Coord c = dwv.coord(ind);
  Cell cell = dwv.readCell(c);

  ent_out[offsets[ind]] = {
    c,
    ELEMENT_COLOR_LUT[cell.elem],
    face_masks[ind]
  };
}

