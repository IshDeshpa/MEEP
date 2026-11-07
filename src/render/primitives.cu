#include "render/primitives.hpp"
#include <cuda_runtime_api.h>
#include <cmath>
#include <utility>

#if !(defined(__CUDA_ARCH__))
#include "extra/check.hpp"
#endif

__host__ __device__ DeviceEntity::DeviceEntity(const DeviceGeometryData *dgd,
                                            Color c, glm::dmat4 t)
    : DeviceEntityData{dgd, nullptr, c, t} {
#if defined(__CUDA_ARCH__)
  triMask = new bool[geom->triangleCount];
#else
  CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&triMask), geom->triangleCount * sizeof(bool)));
  CUDA_CHECK(cudaMemset(triMask, 0, geom->triangleCount * sizeof(bool)));
#endif

  for (std::size_t i = 0; i < geom->triangleCount; ++i) triMask[i] = false;
}

__host__ __device__ DeviceEntity::DeviceEntity(const DeviceGeometryData *dgd,
                                            Color c, worldcoord coords,
                                            glm::dvec3 rot_euler)
    : DeviceEntity(dgd, c, toTransform(coords, rot_euler)) {}

__host__ __device__ const glm::dmat4 DeviceEntity::toTransform(
    glm::dvec3 translation, glm::dvec3 rot_euler) {
  return glm::translate(glm::dmat4(1.0), translation) *
         glm::eulerAngleXYZ(rot_euler.x, rot_euler.y, rot_euler.z);
}

// Scan byte array (prefix-sum) and return total of input array as well
unsigned scanByteNaiveLaunch(DeviceBuffer<uint8_t> &gpu_in, DeviceBuffer<uint32_t> &gpu_out, unsigned blocks, unsigned threads_per_block){
  std::size_t sz = gpu_in.size();
  if(sz == 0) return 0;

  DeviceBuffer<uint32_t> b1(sz);
  DeviceBuffer<uint32_t> b2(sz);
  scanByteNaiveInit<<<blocks, threads_per_block>>>(gpu_in.data(), b1.data(), sz);

  for(uint8_t d=0; d<ceil(log2(sz)); d++){
    scanByteNaiveStep<<<blocks, threads_per_block>>>(b1.data(), b2.data(), d, sz);
    std::swap(b1, b2); // uses move constructor to swap thee two
  }

  gpu_out = b1; // copy constructor will memcpy from b1 to gpu_out only if their types and sizes line up

  unsigned total = b1.get(sz-1) + gpu_in.get(sz-1);

  // b1 and b2 will be cudaFreed automatically (RAII)
  return total;
}

__global__ void scanByteNaiveInit(uint8_t *in, uint32_t *b1, std::size_t sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= sz) return;

  b1[ind] = ind==0?0:in[ind-1];
}

__global__ void scanByteNaiveStep(uint32_t *in, uint32_t *out, uint8_t d, std::size_t sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= sz) return;

  // we only compute the indices past 2^d, where d is the current step of the algo
  std::size_t ofs = std::size_t{1} << d;
  out[ind] = in[ind] + ((ind >= ofs) ? in[ind - ofs] : 0);
}
