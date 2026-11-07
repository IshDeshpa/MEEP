#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <memory>

#include "config.hpp"
#include "extra/cuda_compat.hpp"
#include "extra/cuda_raii.hpp"
#include "render/primitives.hpp"

struct Screen {
  MEEP_HOST_DEVICE Screen(const std::size_t w, const std::size_t h);

  MEEP_HOST_DEVICE Screen(const Screen &other): 
    WIDTH(other.WIDTH),
    HEIGHT(other.HEIGHT),
    framebuffer(other.framebuffer),
    depthbuffer(other.depthbuffer){}

  const std::size_t WIDTH; // pixels
  const std::size_t HEIGHT; // pixels
  Color *framebuffer; // size = w*h
  uint8_t *depthbuffer; // size = w*h
};

static constexpr glm::dmat4x4 makeP(double a, double b, double s, double aspect){
  return {
    s / aspect, 0, 0, 0,
    0, s, 0, 0,
    0, 0, a, -1,
    0, 0, b, 0
  };
}

struct CameraData{
  // Screen data
  const std::size_t WIDTH = CAMERA_SCREEN_WIDTH; // pixels
  const std::size_t HEIGHT = CAMERA_SCREEN_HEIGHT; // pixels
  Color *framebuffer; // size = w*h
  uint8_t *depthbuffer; // size = w*h

  // Will change as we move
  glm::dmat4x4 pose; // transform matrix from camera centered at 0,0,0 in -Z direction

  // Fixed at construction (think like camera's inherent parameters)
  const double Z_NEAR = CAMERA_Z_NEAR;
  const double Z_FAR = CAMERA_Z_FAR;
  const double FOV_DEG = CAMERA_FOV_DEG;

  // For projection (declaration order matters: P is built from A, B, S). Projection matrix is always
  // constant, these params shouldn't change over runtime.
  const double A = (-(Z_FAR + Z_NEAR) / (Z_FAR - Z_NEAR));
  const double B = (-(2 * Z_FAR * Z_NEAR) / (Z_FAR - Z_NEAR));
  const double S = (1.0 / std::tan(glm::radians(FOV_DEG) / 2.0));

  const glm::dmat4x4 P = makeP(A, B, S, double(WIDTH) / HEIGHT);

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

};

class Camera : public CameraData {
public:
  using Data=CameraData;

  // Global singleton, built on first use
  static Camera &instance();

  Camera() = default;
  Camera(const Camera &) = delete;
  Camera &operator=(const Camera &) = delete;

  glm::dmat4x4 syncPose();
private:
  static CameraData *c_gpu;
};

#ifdef __CUDACC__
extern __constant__ Camera::Data *GLOBAL_CAMERA;
#endif
