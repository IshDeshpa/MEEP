#include "render/camera.hpp"
#include "extra/check.hpp"
#include <cstddef>

Camera &Camera::instance(){
  static Camera camera; // host camera instance, constructed the first time this is called
  return camera;
}

__constant__ DeviceScreen::Data DEVICE_SCREEN;

DeviceScreen &DeviceScreen::instance(){
  static DeviceScreen screen(CAMERA_SCREEN_WIDTH, CAMERA_SCREEN_HEIGHT);

  const Data &data = screen;
  CUDA_CHECK(cudaMemcpyToSymbol(DEVICE_SCREEN, &data, sizeof(Data)));

  return screen;
}

DeviceScreen::DeviceScreen(const std::size_t w, const std::size_t h){
  WIDTH = w;
  HEIGHT = h;
  CUDA_CHECK(cudaMalloc(&framebuffer, sizeof(Color) * w * h));
  CUDA_CHECK(cudaMalloc(&depthbuffer, sizeof(uint8_t) * w * h));
}
