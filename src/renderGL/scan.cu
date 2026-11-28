#include "renderGL/scan.hpp"
#include "renderGL/renderGL.hpp"
#include "extra/check.hpp"

#include <cmath>
#include <utility>

__global__ void scanByteNaiveInit(uint8_t *in, uint32_t *out, std::size_t sz);
__global__ void scanByteNaiveStep(uint32_t *in, uint32_t *out, uint8_t d, std::size_t sz);

// Scan byte array (prefix-sum) and return total of input array as well
unsigned scanByteNaiveLaunch(DeviceBuffer<uint8_t> &gpu_in, DeviceBuffer<uint32_t> &gpu_out, unsigned blocks, unsigned threads_per_block){
  RendererGL &r = RendererGL::instance();
  std::size_t sz = gpu_in.size();
  if(sz == 0) return 0;

  DeviceBuffer<uint32_t> b1(sz);
  DeviceBuffer<uint32_t> b2(sz);
  scanByteNaiveInit<<<blocks, threads_per_block, 0, r.render_stream>>>(gpu_in.data(), b1.data(), sz);
  CUDA_CHECK(cudaGetLastError());

  for(uint8_t d=0; d<ceil(log2(sz)); d++){
    scanByteNaiveStep<<<blocks, threads_per_block, 0, r.render_stream>>>(b1.data(), b2.data(), d, sz);
    CUDA_CHECK(cudaGetLastError());
    std::swap(b1, b2); // uses move constructor to swap thee two
  }

  // The scan runs on render_stream; finish it before synchronous copies on
  // the default stream read the result.
  CUDA_CHECK(cudaStreamSynchronize(r.render_stream));
  gpu_out = b1;

  unsigned total = b1.get(sz-1) + (gpu_in.get(sz-1) != 0); // same 0/1 rule as the init kernel

  // b1 and b2 will be cudaFreed automatically (RAII)
  return total;
}

__global__ void scanByteNaiveInit(uint8_t *in, uint32_t *b1, std::size_t sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= sz) return;

  b1[ind] = ind==0?0:(in[ind-1] != 0); // any nonzero input counts as 1
}

__global__ void scanByteNaiveStep(uint32_t *in, uint32_t *out, uint8_t d, std::size_t sz){
  std::size_t ind = blockIdx.x * blockDim.x + threadIdx.x;
  if(ind >= sz) return;

  // we only compute the indices past 2^d, where d is the current step of the algo
  std::size_t ofs = std::size_t{1} << d;
  out[ind] = in[ind] + ((ind >= ofs) ? in[ind - ofs] : 0);
}

