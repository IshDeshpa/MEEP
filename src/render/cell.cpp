#include "render/cell.hpp"
#include "render/primitives.hpp"
#include "state/state.hpp"

#include <map>
#include <algorithm>
#include <memory>
#include <stddef.h>
#include <glm/gtx/component_wise.hpp>
#include <stdexcept>

static inline Cell &getAdjCell(CellRef &c, WorldState &ws, Direction d){
  return ws(c.coords + coord(lutGet(DIRECTION_LUT, d)));
}

static inline bool isFaceExposed(CellRef &c, WorldState &ws, const CellFace &face){
  return getAdjCell(c, ws, face.d).elem == Element::AIR;
}

// A face is renderable when its cell is not AIR and its neighbor is AIR.
static inline bool isRenderable(CellRef &c, WorldState &ws, const CellFace &face){
  return c.cell.elem != Element::AIR && isFaceExposed(c, ws, face);
}

static inline bool isAnyFaceRenderable(CellRef &c, WorldState &ws){
  for(const CellFace &face : CellRenderEntity::CUBE_FACES){
    if(isRenderable(c, ws, face)) return true;
  }
  return false;
}

// Generate CellRenderEntity only for those cells that could possibly
// be rendered (track state of any non-obstructed cell)
std::unique_ptr<CellRenderEntity> CellRenderEntity::create(CellRef &c, WorldState &ws){
  if(!isAnyFaceRenderable(c, ws)) return nullptr; // consider non-renderable cells as obstructed

  return std::unique_ptr<CellRenderEntity>(new CellRenderEntity(c));
}

unsigned int CellRenderEntity::updateMeshMask(WorldState &ws){
  meshMask.reserve(mesh.last - mesh.first + 1);

  for(const CellFace &face : CUBE_FACES){
    if(isRenderable(this->cell_ref, ws, face)){
      this->meshMask[face.a] = true;
      this->meshMask[face.b] = true;
    }
  }

  return std::count(this->meshMask.begin(), this->meshMask.end(), true);
}

void CellRenderEntity::updateColor(){
  this->color = lutGet(ELEMENT_COLOR_LUT, this->cell_ref.cell.elem);
}

const std::array<CellFace, 6> CellRenderEntity::makeFaces(){
  std::array<CellFace, 6> arr;

  for(unsigned int i=0; i<CUBE_GEOM.size(); i++){
    Tri t = CUBE_GEOM.tris[i];

    // What direction is the triangle facing?
    // Winding determines facing; CCW is dir it faces
    dvec3 v0, v1, v2; 
    v0 = CUBE_GEOM.verts[t.x];
    v1 = CUBE_GEOM.verts[t.y];
    v2 = CUBE_GEOM.verts[t.z];

    dvec3 normal = normalize(cross(v1 - v0, v2 - v0)); // this points in the direction we're facing
    if(compAdd(abs(normal)) != 1) throw std::runtime_error("Invalid normal");
    
    ivec3 normal_int = static_cast<ivec3>(normal);
    int key = normal_int.x + 2 * normal_int.y + 3 * normal_int.z;
    Direction d;
    switch(key){
      case 1: d = Direction::X_PLUS; break;
      case -1: d = Direction::X_MINUS; break;
      case 2: d = Direction::Y_PLUS; break;
      case -2: d = Direction::Y_MINUS; break;
      case 3: d = Direction::Z_PLUS; break;
      case -3: d = Direction::Z_MINUS; break;
      default: throw std::runtime_error("Invalid direction");
    }

    unsigned int imax = std::numeric_limits<unsigned int>().max();
    if(arr[static_cast<unsigned int>(d)].a == imax){
      arr[static_cast<unsigned int>(d)].a = static_cast<TriRef>(i);
    } else if(arr[static_cast<unsigned int>(d)].b == imax){
      arr[static_cast<unsigned int>(d)].a = static_cast<TriRef>(i);
    } else {
      throw std::runtime_error("More than two triangles for a face?");
    }
    arr[static_cast<unsigned int>(d)].d = d;
  }

  return arr;
}
