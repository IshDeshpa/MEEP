#include "render/cell.hpp"
#include "render/render.hpp"
#include <cuda_runtime_api.h>
#include <cstdio>
#include <driver_types.h>

Renderer &Renderer::instance(){
  static Renderer renderer;
  return renderer;
}

Renderer::Renderer()
    : device_cube(&DeviceCube::instance()),
      device_screen(&DeviceScreen::instance()) {
  CUDA_CHECK(cudaStreamCreate(&render_stream));
}

Renderer::~Renderer() {
  if (render_stream) cudaStreamDestroy(render_stream);
}
