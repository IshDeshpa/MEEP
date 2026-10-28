#pragma once

#include <cuda_runtime_api.h>

#include <stdexcept>
#include <string>

namespace meep {

// Host-side check only: this does not wait for asynchronous GPU work.
inline void cuda_check(cudaError_t status, const char* call,
                       const char* file, int line) {
    if (status == cudaSuccess)
        return;

    throw std::runtime_error(
        std::string(file) + ":" + std::to_string(line) +
        ": CUDA call " + call + " failed: " +
        cudaGetErrorName(status) + " (" + cudaGetErrorString(status) + ")");
}

} // namespace meep

// Evaluate call once and retain the caller's source location.
#define CUDA_CHECK(call) ::meep::cuda_check((call), #call, __FILE__, __LINE__)
