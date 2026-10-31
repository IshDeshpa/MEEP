#pragma once

// Preserve CUDA annotations for nvcc; ordinary C++ compilers see declarations
// without execution-space annotations.
#if defined(__CUDACC__)
#define MEEP_DEVICE __device__
#define MEEP_HOST_DEVICE __host__ __device__
#else
#define MEEP_DEVICE
#define MEEP_HOST_DEVICE
#endif
