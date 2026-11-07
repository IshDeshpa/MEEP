#pragma once
#include "extra/cuda_compat.hpp"
#include "extra/cuda_raii.hpp"
#include "render/cell.hpp"
#include <cstddef>
#include <memory>

class Renderer{
public:
  CellRenderEntity *to_render;
  std::unique_ptr<DeviceBuffer<CellRenderEntity>> entities;

  // Global singleton, built on first use
  static Renderer &instance();

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

private:
  Renderer() = default;
};

// 1. State -> world-space vertices, triangles, and colors
void stateToWorldLaunch(WorldState &ws);
MEEP_GLOBAL void isVisible(DeviceWorldView dwv, uint8_t *is_visible);
// Create cell references
MEEP_GLOBAL void createRefsEntities(DeviceWorldView dwv, const uint8_t *is_visible, const uint32_t *offsets, CellRenderEntity *ent_out, std::size_t world_sz);

// 2. World -> clip space AND clip the triangles against the near plane
void worldToClipLaunch();
MEEP_GLOBAL void generateTransforms(const CellRenderEntity *entities, std::size_t sz, glm::dmat4x4 *transforms, glm::dmat4x4 VP);

// 3. Clip space -> NDC
MEEP_GLOBAL void clipToNdc();

// 4. NDC -> screen
MEEP_GLOBAL void ndcToScreen();

// 5. Rasterize the screen (assign colors to pixels)
MEEP_GLOBAL void rasterize();
