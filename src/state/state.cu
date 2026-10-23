#include "state.hpp"
#include <cuda_runtime.h>

#include <limits>
#include <stdexcept>

WorldState::WorldState(dim_t n, dim_t m, dim_t k)
    : n(n), m(m), k(k), curr(nullptr), next(nullptr) {
    const auto max = std::numeric_limits<dim_t>::max();
    if (n == 0 || m == 0 || k == 0) {
        throw std::invalid_argument("World dimensions must be positive");
    }
    if (n > max / m || n * m > max / k ||
        n * m * k > max / sizeof(Cell)) {
        throw std::overflow_error("World allocation size overflows");
    }

    const auto bytes = n * m * k * sizeof(Cell);
    auto error = cudaMallocManaged(&curr, bytes);
    if (error != cudaSuccess) {
        throw std::runtime_error(cudaGetErrorString(error));
    }

    error = cudaMallocManaged(&next, bytes);
    if (error != cudaSuccess) {
        cudaFree(curr);
        throw std::runtime_error(cudaGetErrorString(error));
    }
}

WorldState::~WorldState() {
    cudaFree(curr);
    cudaFree(next);
}

dim_t WorldState::index(dim_t x, dim_t y, dim_t z) const {
    return x + n * (y + m * z);
}
