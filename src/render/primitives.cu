#include "render/primitives.hpp"
#include <cuda_runtime_api.h>
#include <stdexcept>

#if !(defined(__CUDA_ARCH__))
#include "extra/check.hpp"
#endif

__host__ __device__ DeviceEntity::DeviceEntity(const DeviceGeometryData &dgd,
                                            Color c, glm::dmat4 t)
    : DeviceEntityData{dgd, nullptr, c, t} {
#if defined(__CUDA_ARCH__)
        triMask = new bool[geom.triangleCount];
#else
        CUDA_CHECK(cudaMalloc(&triMask, geom.triangleCount));
#endif

  for (std::size_t i = 0; i < geom.triangleCount; ++i) triMask[i] = false;
}

__host__ __device__ DeviceEntity::DeviceEntity(const DeviceGeometryData &dgd,
                                            Color c, worldcoord coords,
                                            glm::dvec3 rot_euler)
    : DeviceEntity(dgd, c, toTransform(coords, rot_euler)) {}

__host__ __device__ glm::dmat4 DeviceEntity::toTransform(
    glm::dvec3 translation, glm::dvec3 rot_euler) {
  return glm::translate(glm::dmat4(1.0), translation) *
         glm::eulerAngleXYZ(rot_euler.x, rot_euler.y, rot_euler.z);
}

