#include "render/render.hpp"
#include "state/state.hpp"
#include <memory>
#include <stddef.h>

// Conditions for being able to render a block:
// 1) inside view of camera
// 2) not AIR
static inline bool isRenderable(CellRef &c){
  return (c.cell.elem != Element::AIR);
}

static inline Cell &getAdjCell(CellRef &c, WorldState &ws, Direction d){
  return ws(c.coords + lutGet(DIRECTION_LUT, d));
}

static inline bool isFaceExposed(CellRef &c, WorldState &ws, Direction d){
  return getAdjCell(c, ws, d).elem != Element::AIR;
}

static inline bool isAnyFaceExposed(CellRef &c, WorldState &ws){
  for(uint8_t i=0; i<static_cast<uint8_t>(Direction::COUNT); i++){
    if(isFaceExposed(c, ws, static_cast<Direction>(i))) return true;
  }
  return false;
}

// Generate CellRenderEntity only for those cells that could possibly
// be rendered (track state of any non-obstructed cell)
// std::unique_ptr<CellRenderEntity> CellRenderEntity::create(CellRef &c, WorldState &ws){
//   if(!isRenderable(c)) return nullptr; // consider non-renderable cell as obstructed
//   if(isAnyFaceExposed(c, ws)) return nullptr; // consider anything fully surrounded as obstructed
//
// }

// To allocate full world treangle vector, we must identify all triangles 
// that are necessary to render, and size the vector to that count.

