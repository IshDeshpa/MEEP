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

// 2. State -> world-space vertices, triangles, and colors
void stateToWorldLaunch(WorldState &ws);
MEEP_GLOBAL void isVisible(DeviceWorldView dwv, uint8_t *is_visible);

// Create cell references
MEEP_GLOBAL void createRefsEntities(DeviceWorldView dwv, uint8_t *is_visible, uint32_t *offsets, CellRenderEntity *ent_out, std::size_t world_sz);

// 3. World -> clip space
void worldToClipLaunch();
MEEP_GLOBAL void worldToClip();
// 4. Clip the triangles against the near plane
void clipNearPlaneLaunch();
MEEP_GLOBAL void clipNearPlane();
// 5. Clip space -> NDC
MEEP_GLOBAL void clipToNdc();
// 6. NDC -> screen
MEEP_GLOBAL void ndcToScreen();
// 7. Rasterize the screen (assign colors to pixels)
MEEP_GLOBAL void rasterize();
