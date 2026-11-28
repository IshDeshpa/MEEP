#include "extra/check.hpp"
#include "render/render.hpp"
#include "state/state.hpp"

#include <cuda_runtime.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

static void checkMasks(const WorldState &world) {
  const auto &renderer = Renderer::instance();
  const auto &geometry = *HostCube::instance().CUBE_GEOM.host();
  const auto count = renderer.entities->size();
  const auto triangles = geometry.tris.size();
  require(renderer.tri_masks->size() == count * triangles, "Incorrect mask allocation size");
  std::vector<CellRenderEntity> entities(count);
  std::vector<unsigned char> masks(renderer.tri_masks->bytes());
  CUDA_CHECK(cudaMemcpy(entities.data(), renderer.entities->data(),
                        renderer.entities->bytes(), cudaMemcpyDeviceToHost));
  CUDA_CHECK(cudaMemcpy(masks.data(), renderer.tri_masks->data(),
                        masks.size(), cudaMemcpyDeviceToHost));
  static_assert(sizeof(bool) == 1);
  for (std::size_t index = 0; index < count; ++index) {
    require(entities[index].triMask == renderer.tri_masks->data() + index * triangles,
            "Entity mask must reference its own renderer-owned slice");
    const Coord cell(entities[index].transform[3]);
    require(cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
            cell.x < static_cast<int>(world.n) && cell.y < static_cast<int>(world.m) &&
            cell.z < static_cast<int>(world.k),
            "Invalid entity coordinates");
    require(world(cell).elem == Element::WATER, "Unexpected sphere material");
    for (std::size_t triangle = 0; triangle < triangles; ++triangle) {
      const auto indices = geometry.tris[triangle];
      const glm::dvec3 a(geometry.verts[indices.x]);
      const glm::dvec3 b(geometry.verts[indices.y]);
      const glm::dvec3 c(geometry.verts[indices.z]);
      const auto normal = glm::cross(b - a, c - a);
      int axis = 0;
      if (std::abs(normal.y) > std::abs(normal[axis])) axis = 1;
      if (std::abs(normal.z) > std::abs(normal[axis])) axis = 2;
      auto adjacent = cell;
      adjacent[axis] += normal[axis] > 0 ? 1 : -1;
      const bool exposed = adjacent.x < 0 || adjacent.y < 0 || adjacent.z < 0 ||
          adjacent.x >= static_cast<int>(world.n) || adjacent.y >= static_cast<int>(world.m) ||
          adjacent.z >= static_cast<int>(world.k) ||
          world(adjacent).elem == Element::AIR;
      require(masks[index * triangles + triangle] == exposed,
              "Triangle mask disagrees with the CPU face-visibility reference");
    }
  }
}

int main() {
  constexpr std::size_t captures = 1024;
  constexpr std::size_t render_passes = 8;
  std::size_t free_bytes, total_bytes;
  CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
  std::cout << "20x20x20 sphere: world buffers=21296 bytes, LUT=0, free VRAM=" << free_bytes << '\n';
  WorldState world(20, 20, 20);
  for (int x = 0; x < 20; ++x) for (int y = 0; y < 20; ++y) for (int z = 0; z < 20; ++z) {
    if ((x - 10) * (x - 10) + (y - 10) * (y - 10) + (z - 10) * (z - 10) <= 25)
      world.world[world.index(x, y, z)].elem = Element::WATER;
  }
  const auto world_bytes = world.n_pad * world.m_pad * world.k_pad * sizeof(Cell);
  CUDA_CHECK(cudaMemcpy(world.d_curr, world.world, world_bytes, cudaMemcpyHostToDevice));
  for (std::size_t frame = 0; frame < captures; ++frame) {
    require(stateToWorldLaunch(world), "Expected a nonempty sphere snapshot");
    require(Renderer::instance().entities->size() == 222, "Unexpected visible sphere-cell count");
    if (frame % 128 == 0 || frame + 1 == captures) checkMasks(world);
  }

  Camera::instance().setPose(glm::dvec3(10, 10, -15), glm::dvec3(0, 0, 1));
  for (std::size_t frame = 0; frame < render_passes; ++frame) {
    require(stateToWorldLaunch(world), "Expected visible entities before rendering");
    worldToClipLaunch();
    clipVertsLaunch();
    clipToNDCLaunch();
    ndcToScreenLaunch();
    rasterizeLaunch();
    checkMasks(world);
  }

  CUDA_CHECK(cudaMemGetInfo(&free_bytes, &total_bytes));
  std::cout << "4x4x4 blank: world buffers=432 bytes, LUT=0, free VRAM=" << free_bytes << '\n';
  WorldState blank(4, 4, 4);
  require(!stateToWorldLaunch(blank), "Expected an empty snapshot");
  require(!Renderer::instance().entities && !Renderer::instance().tri_masks,
          "Empty snapshots must release entities and triangle masks");
  require(stateToWorldLaunch(world), "Failed to recreate masks after an empty snapshot");
  checkMasks(world);
  std::cout << "PASS: " << captures << " snapshots, " << render_passes
            << " render passes, CPU-reference triangle masks, empty/recreated mask ownership\n";
}
