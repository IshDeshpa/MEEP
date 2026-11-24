#include "render/camera.hpp"
#include "extra/check.hpp"
#include <cstddef>
#include <fstream>
#include <stdexcept>

const glm::dvec3 Camera::UP(0.0, 1.0, 0.0);

void HostScreen::writePPM(const std::string &path) const {
  std::ofstream image(path, std::ios::binary);
  if (!image) {
    throw std::runtime_error("Cannot open " + path);
  }

  image << "P6\n" << WIDTH << ' ' << HEIGHT << "\n255\n";
  for (const Color &pixel : framebuffer) {
    const unsigned char rgb[3] = {pixel.r, pixel.g, pixel.b};
    image.write(reinterpret_cast<const char *>(rgb), sizeof(rgb));
  }

  if (!image) {
    throw std::runtime_error("Failed to write " + path);
  }
}

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
