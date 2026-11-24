#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/matrix.hpp>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <string>
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
  std::vector<float> depthbuffer; // size = w*h

  void writePPM(const std::string &path) const;
};

// GPU-resident screen data: the buffers are device pointers
struct DeviceScreenData {
  std::size_t WIDTH; // pixels
  std::size_t HEIGHT; // pixels
  Color *framebuffer; // size = w*h
  float *depthbuffer; // size = w*h
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

  static const glm::dvec3 UP;

  void setPose(const glm::dvec3& new_pos, const glm::dvec3& new_look_dir) {
    std::lock_guard lock(pose_mutex);
    pos = new_pos;
    look_dir = new_look_dir;
  }

  void moveForward(double distance) {
    std::lock_guard lock(pose_mutex);
    pos += glm::normalize(look_dir) * distance;
  }

  void moveRight(double distance) {
    std::lock_guard lock(pose_mutex);
    pos += glm::cross(glm::normalize(look_dir), UP) * distance;
  }

  void rotateLookDir(int dx, int dy) {
    std::lock_guard lock(pose_mutex);
    glm::dvec3 forward = glm::normalize(look_dir);
    forward = glm::rotate(forward, -dx * MOUSE_SENSITIVITY, UP);

    const glm::dvec3 right = glm::normalize(glm::cross(forward, UP));
    const glm::dvec3 candidate =
        glm::normalize(glm::rotate(forward, -dy * MOUSE_SENSITIVITY, right));
    if (std::abs(glm::dot(candidate, UP)) < 0.99) {
      forward = candidate;
    }

    look_dir = forward;

#ifdef MEEP_DEBUG
    std::printf("look: %0.2f, %0.2f, %0.2f\n", look_dir.x, look_dir.y, look_dir.z);
#endif
  }

  constexpr glm::dvec3 getPos(){
    return this->pos;
  }

  // Fixed at construction (think like camera's inherent parameters)
  const float Z_NEAR = CAMERA_Z_NEAR;
  const float Z_FAR = CAMERA_Z_FAR;
  const float FOV_DEG = CAMERA_FOV_DEG;

  // For projection (declaration order matters: P is built from A, B, S). Projection matrix is always
  // constant, these params shouldn't change over runtime.
  const float A = (-(Z_FAR + Z_NEAR) / (Z_FAR - Z_NEAR));
  const float B = (-(2 * Z_FAR * Z_NEAR) / (Z_FAR - Z_NEAR));
  const float S = (1.0 / std::tan(glm::radians(FOV_DEG) / 2.0));

  const float ASPECT = float(SCREEN_WIDTH) / SCREEN_HEIGHT;

  const glm::dmat4x4 P = makeP(A, B, S, ASPECT);

  MEEP_HOST_DEVICE const glm::dmat4x4 getV() const {
#ifdef __CUDA_ARCH__
    return glm::lookAt(pos, pos + look_dir, glm::dvec3(0.0, 1.0, 0.0));
#else
    std::lock_guard lock(pose_mutex);
    return glm::lookAt(pos, pos + look_dir, UP);
#endif
  }

  MEEP_HOST_DEVICE constexpr glm::dmat4x4 getP() const {
    return P;
  }

  MEEP_HOST_DEVICE const glm::dmat4x4 getVP() const{
    return getP() * getV();
  }

private:
  Camera() : HostScreen(SCREEN_WIDTH, SCREEN_HEIGHT) {}

  // Position and look direction must only be accessed while pose_mutex is held.
  mutable std::mutex pose_mutex;
  glm::dvec3 pos{0.0, 0.0, 0.0};
  glm::dvec3 look_dir{0.0, 0.0, 1.0};
};
