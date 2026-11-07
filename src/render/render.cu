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
  renderer.device_cube = &DeviceCube::instance(); // initialize the DeviceCube singleton on GPU side (the DeviceCube constructor allocates all the device-side buffers)
  DeviceScreen::instance(); // initialize the DeviceScreen singleton and copy it into DEVICE_SCREEN

  return renderer;
}
