#pragma once

// Preserve CUDA annotations for nvcc; ordinary C++ compilers see declarations
// without execution-space annotations.
#if defined(__CUDACC__)
#define MEEP_DEVICE __device__
#define MEEP_HOST_DEVICE __host__ __device__
#define MEEP_GLOBAL __global__
#else
#define MEEP_DEVICE
#define MEEP_HOST_DEVICE
#define MEEP_GLOBAL
#endif
