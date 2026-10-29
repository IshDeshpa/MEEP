#include "extra/check.hpp"

#include <cuda_runtime_api.h>
#include <stdexcept>
#include <string>

namespace meep {

void cuda_check(int status, std::string call, std::string file, int line) {
    if (status == cudaSuccess)
        return;

    throw std::runtime_error(
        file + ":" + std::to_string(line) +
        ": CUDA call " + call + " failed: " +
        cudaGetErrorName(static_cast<cudaError_t>(status)) + " (" +
        cudaGetErrorString(static_cast<cudaError_t>(status)) + ")");
}

} // namespace meep
