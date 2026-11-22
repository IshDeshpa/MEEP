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
