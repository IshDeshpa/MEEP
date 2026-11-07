#include "render/camera.hpp"
#include "extra/check.hpp"
#include <cstddef>
#include <glm/fwd.hpp>

__constant__ Camera::Data *GLOBAL_CAMERA = nullptr; // device pointer to devicce camera instance
CameraData *Camera::c_gpu = nullptr; // host pointer to device camera instance

Camera &Camera::instance(){
  static Camera camera; // host camera instance, constructed the first time this is called

  if(c_gpu == nullptr) {
    CUDA_CHECK(cudaMalloc(&c_gpu, sizeof(CameraData))); // instantiate GPU copy of camera
    CUDA_CHECK(cudaMemcpyToSymbol(GLOBAL_CAMERA, &c_gpu, sizeof(CameraData*)));
  }

  return camera;
}

__host__ __device__ Screen::Screen(const std::size_t w, const std::size_t h) : WIDTH(w), HEIGHT(h){
#ifdef __CUDA_ARCH__
  framebuffer = new Color[w * h];
  depthbuffer = new uint8_t[w * h];
#else
  CUDA_CHECK(cudaMalloc(&framebuffer, sizeof(Color) * w * h));
  CUDA_CHECK(cudaMalloc(&depthbuffer, sizeof(uint8_t) * w * h));
#endif
}

// syncs the current pose to the device and returns the synced pose in one operation
glm::dmat4x4 Camera::syncPose(){
  void *d_pose = reinterpret_cast<char *>(c_gpu) + offsetof(CameraData, pose); // byte offset into the device copy
  CUDA_CHECK(cudaMemcpy(d_pose, &pose, sizeof(pose), cudaMemcpyHostToDevice));
  return this->getVP();
}
