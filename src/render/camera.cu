#include "render/camera.hpp"
#include "extra/check.hpp"
#include <cstddef>

const glm::dvec3 Camera::UP(0.0, 1.0, 0.0);

Camera &Camera::instance(){
  static Camera camera; // host camera instance, constructed the first time this is called
  return camera;
}

__constant__ DeviceScreen::Data DEVICE_SCREEN;

DeviceScreen &DeviceScreen::instance(){
  static DeviceScreen screen(SCREEN_WIDTH, SCREEN_HEIGHT);

  const Data &data = screen;
  CUDA_CHECK(cudaMemcpyToSymbol(DEVICE_SCREEN, &data, sizeof(Data)));

  return screen;
}

DeviceScreen::DeviceScreen(const std::size_t w, const std::size_t h){
  WIDTH = w;
  HEIGHT = h;
  CUDA_CHECK(cudaMalloc(&framebuffer, sizeof(Color) * w * h));
  CUDA_CHECK(cudaMalloc(&depthbuffer, sizeof(float) * w * h));
}

HostScreen DeviceScreen::hostScreen() const {
  HostScreen screen(WIDTH, HEIGHT);
  const std::size_t pixels = WIDTH * HEIGHT;
  CUDA_CHECK(cudaMemcpy(screen.framebuffer.data(), framebuffer,
                        pixels * sizeof(Color), cudaMemcpyDeviceToHost));
  CUDA_CHECK(cudaMemcpy(screen.depthbuffer.data(), depthbuffer,
                        pixels * sizeof(float), cudaMemcpyDeviceToHost));
  return screen;
}
