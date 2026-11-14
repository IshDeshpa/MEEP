#pragma once
#include "extra/cuda_compat.hpp"
#include "extra/cuda_raii.hpp"
#include "render/camera.hpp"
#include "render/cell.hpp"
#include <cstddef>
#include <memory>

inline constexpr std::size_t MAX_NEW_VERTS = 12;
inline constexpr std::size_t VERTS_PER_CUBE = 8 + MAX_NEW_VERTS;

class Renderer{
public:
  const DeviceCube *device_cube;

  std::unique_ptr<DeviceBuffer<CellRenderEntity>> entities;

  std::unique_ptr<DeviceBuffer<Vertex>> verts;
  std::unique_ptr<DeviceBuffer<Tri>> tris;

  std::unique_ptr<DeviceBuffer<Vertex2D>> verts2d;

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
MEEP_GLOBAL void createRefsEntities(DeviceWorldView dwv, const uint8_t *is_visible, const uint32_t *offsets, CellRenderEntity *ent_out, std::size_t world_sz);

// 2. World -> clip space 
void worldToClipLaunch();
MEEP_GLOBAL void generateTris(const CellRenderEntity *entities, Vertex *verts, Tri *tris, std::size_t num_entities, glm::dmat4x4 vp);

// 3. Clip the triangles against the NEAR PLANE ONLY
void clipVertsLaunch();
// Take in verts & tris, invalidate clipped verts & reconstruct tris that include those verts
// Logic: 
// If 1 vert is behind near plane, we clip: invalidate clipped vert and create two new verts (at intersection points w/ near plane). 
//  old tri is modified, and new tri is added.
// If 2 verts are behind near plane, we clip: both verts locations change to new locations (at intersection points w/ near plane).
// if all 3 verts in a tri are behind near plane, we invalidate the triangle entirely (set all indices to UINT_MAX?)
MEEP_GLOBAL void clipVertsByNear(Vertex *verts, Tri *tris, std::size_t num_entities);

// 4. Clip space -> NDC
void clipToNDCLaunch();
// (x, y, z, w) -> (x/w, y/w, z/w, 1) for every vert. w > Z_near (not 0) in all cases here hopefully (if we clipped right)
// Might be able to combine with clipVertsByNear, but we'll do that later
MEEP_GLOBAL void clipToNDC(Vertex *verts, std::size_t num_entities);
MEEP_GLOBAL void backfaceCull(const Vertex *verts, Tri *tris, std::size_t tris_sz);

// 5. NDC -> screen
// maps 3d NDC coords to 2d screen coords
// probably does more clipping too
void ndcToScreenLaunch();
MEEP_GLOBAL void ndcToScreen(const Vertex *verts, Vertex2D *verts2d, std::size_t count);

// 6. Rasterize the screen (assign colors to pixels)
// Turn verts into pixels + depth buffer in the on-gpu DeviceScreen, and then cudaMemcpy over to the on-cpu HostScreen
// At the end of this, in the HostScreen Vectors, we should have exactly the final pixels to put on the screen
void rasterizeLaunch();
MEEP_GLOBAL void rasterize();
