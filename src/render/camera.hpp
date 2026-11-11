#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <cstddef>
#include <vector>

#include "config.hpp"
#include "extra/cuda_compat.hpp"
#include "render/primitives.hpp"

// CPU-side screen: the framebuffer and depth buffer are host vectors
struct HostScreen {
  HostScreen(const std::size_t w, const std::size_t h) : WIDTH(w), HEIGHT(h), framebuffer(w * h), depthbuffer(w * h){}

  const std::size_t WIDTH; // pixels
  const std::size_t HEIGHT; // pixels
  std::vector<Color> framebuffer; // size = w*h
  std::vector<uint8_t> depthbuffer; // size = w*h
};

// GPU-resident screen data: the buffers are device pointers
struct DeviceScreenData {
  std::size_t WIDTH; // pixels
  std::size_t HEIGHT; // pixels
  Color *framebuffer; // size = w*h
  uint8_t *depthbuffer; // size = w*h
};

// Host-side handle that allocates the GPU buffers and copies DeviceScreenData into DEVICE_SCREEN
class DeviceScreen : public DeviceScreenData {
public:
  using Data = DeviceScreenData;

  // Global singleton, built on first use. Not const: the buffers are written to by kernels.
  static DeviceScreen &instance();

  DeviceScreen(const DeviceScreen &) = delete;
  DeviceScreen &operator=(const DeviceScreen &) = delete;

private:
  DeviceScreen(const std::size_t w, const std::size_t h);
};

#ifdef __CUDACC__
extern __constant__ DeviceScreen::Data DEVICE_SCREEN;
#endif

static constexpr glm::dmat4x4 makeP(float a, float b, float s, float aspect){
  return {
    s / aspect, 0, 0, 0,
    0, s, 0, 0,
    0, 0, a, -1,
    0, 0, b, 0
  };
}

class Camera : public HostScreen {
public:
  // Global singleton, built on first use
  static Camera &instance();

  Camera(const Camera &) = delete;
  Camera &operator=(const Camera &) = delete;

  // Will change as we move
  glm::dmat4x4 pose = glm::dmat4x4(1.0); // transform matrix from camera centered at 0,0,0 in -Z direction (identity: at the origin, looking down -Z)

  // Fixed at construction (think like camera's inherent parameters)
  const float Z_NEAR = CAMERA_Z_NEAR;
  const float Z_FAR = CAMERA_Z_FAR;
  const float FOV_DEG = CAMERA_FOV_DEG;

  // For projection (declaration order matters: P is built from A, B, S). Projection matrix is always
  // constant, these params shouldn't change over runtime.
  const float A = (-(Z_FAR + Z_NEAR) / (Z_FAR - Z_NEAR));
  const float B = (-(2 * Z_FAR * Z_NEAR) / (Z_FAR - Z_NEAR));
  const float S = (1.0 / std::tan(glm::radians(FOV_DEG) / 2.0));

  const float ASPECT = float(CAMERA_SCREEN_WIDTH) / CAMERA_SCREEN_HEIGHT;

  const glm::dmat4x4 P = makeP(A, B, S, ASPECT);

  // w=0 makes this a direction, so the pose's translation does not affect it
  MEEP_HOST_DEVICE const glm::dmat4x4 getV() const {
    return glm::inverse(pose);
  }

  MEEP_HOST_DEVICE constexpr glm::dmat4x4 getP() const {
    return P;
  }

  MEEP_HOST_DEVICE const glm::dmat4x4 getVP() const{
    return getP() * getV();
  }

private:
  Camera() : HostScreen(CAMERA_SCREEN_WIDTH, CAMERA_SCREEN_HEIGHT) {}
};
