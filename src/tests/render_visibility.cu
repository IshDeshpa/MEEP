#include "render/render.hpp"
#include "extra/check.hpp"

#include <array>
#include <cstdint>
#include <iostream>

int main() {
  constexpr std::size_t count = 125;
  std::array<uint8_t, count> input{};
  input[0] = 1;
  input[42] = 63;
  input[124] = 2;
  DeviceBuffer<uint8_t> gpu_in(count);
  DeviceBuffer<uint32_t> gpu_out(count);
  CUDA_CHECK(cudaMemcpy(gpu_in.data(), input.data(), count, cudaMemcpyHostToDevice));
  if (scanByteNaiveLaunch(gpu_in, gpu_out, 1, 256) != 3 ||
      gpu_out.get(0) != 0 || gpu_out.get(42) != 1 || gpu_out.get(124) != 2) {
    std::cerr << "Visibility scan failed\n";
    return 1;
  }

  WorldState world(5, 5, 5);
  if (stateToWorldLaunch(world)) {
    std::cerr << "Blank world unexpectedly has visible cells\n";
    return 1;
  }
  world.world[world.index(2, 2, 2)] = Cell{Element::LAVA, 0};
  CUDA_CHECK(cudaMemcpy(world.d_curr, world.world,
                        world.n_pad * world.m_pad * world.k_pad * sizeof(Cell),
                        cudaMemcpyHostToDevice));
  if (!stateToWorldLaunch(world) || Renderer::instance().entities->size() != 1) {
    std::cerr << "Populated world did not produce one visible cell\n";
    return 1;
  }
  std::cout << "Visibility scan and world rendering checks passed\n";
}
