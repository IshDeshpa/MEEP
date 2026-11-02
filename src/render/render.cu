#include "extra/cuda_raii.hpp"
#include "render/cell.hpp"
#include "render/render.hpp"
#include "state/state.hpp"
#include <cuda_runtime_api.h>
#include <cstdint>
#include <cstdio>
#include <driver_types.h>

Renderer &Renderer::instance(){
  static Renderer renderer;
  // Do your const inits here
  [[maybe_unused]] static const DeviceCube &device_cube = DeviceCube::instance(); // initialize the DeviceCube singleton on GPU side
  return renderer;
}
