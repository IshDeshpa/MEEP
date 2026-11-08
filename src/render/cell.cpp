#include "render/cell.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>
#include <cstdio>
#include <stdexcept>

void CellRenderEntity::print() const {
  const CellRef &cr = cell_ref;
  std::printf("CellRenderEntity{elem=%u state=%u coords=(%d %d %d) color=(%u %u %u %u)}\n",
    static_cast<unsigned>(cr.cell.elem), static_cast<unsigned>(cr.cell.state),
    cr.coords.x, cr.coords.y, cr.coords.z,
    static_cast<unsigned>(color.r), static_cast<unsigned>(color.g),
    static_cast<unsigned>(color.b), static_cast<unsigned>(color.a));
}

HostCube &HostCube::instance(){
  static HostCube cube;
  return cube;
}

std::size_t HostCube::numTris(){
  return instance().CUBE_GEOM.host()->tris.size();
}

std::size_t HostCube::numVerts(){
  return instance().CUBE_GEOM.host()->verts.size();
}

const std::array<CubeFace, 6> HostCube::makeFaces() const{
  std::array<CubeFace, 6> arr;

  // Faces are classified from the CPU-side copy that CUBE_GEOM keeps.
  const HostGeometryData &geom = *CUBE_GEOM.host();

  // loop through all tris and assign them around
  for(unsigned int i=0; i<geom.tris.size(); i++){
    Tri t = geom.tris[i];

    // What direction is the triangle facing?
    // Winding determines facing; CCW is dir it faces
    glm::dvec3 v0, v1, v2;
    v0 = geom.verts[t.x];
    v1 = geom.verts[t.y];
    v2 = geom.verts[t.z];

    glm::dvec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0)); // this points in the direction we're facing
    if(glm::compAdd(glm::abs(normal)) != 1) throw std::runtime_error("Invalid normal");

    glm::ivec3 normal_int = static_cast<glm::ivec3>(normal);
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
      arr[static_cast<unsigned int>(d)].b = static_cast<TriRef>(i);
    } else {
      throw std::runtime_error("More than two triangles for a face?");
    }
    arr[static_cast<unsigned int>(d)].d = d;
  }

  return arr;
}
