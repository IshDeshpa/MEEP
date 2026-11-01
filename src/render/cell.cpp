#include "render/cell.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/component_wise.hpp>
#include <stdexcept>

HostCube &HostCube::instance(){
  static HostCube cube;
  return cube;
}

const std::array<CubeFace, 6> HostCube::makeFaces() const{
  std::array<CubeFace, 6> arr;

  // Faces are classified from the CPU-side copy that CUBE_GEOM keeps.
  const HostGeometryData &geom = CUBE_GEOM.host();

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






// #include "render/primitives.hpp"
// #include "state/state.hpp"
// #include <map>
// #include <algorithm>
// #include <cstdio>
// #include <memory>
// #include <stddef.h>
// #include <glm/gtx/component_wise.hpp>
// #include <stdexcept>
//
//
// unsigned int CellRenderEntity::updateTriangleMask(WorldState &ws){
//   triangleMask.resize(geom.size());
//
//   const auto mark_triangle = [this](TriRef triangle) {
//     if(triangle >= geom.size()){
//       throw std::out_of_range("Cell face triangle is outside its geometry");
//     }
//     triangleMask[triangle] = true;
//   };
//
//   for(const CellFace &face : CUBE_FACES){
//     if(isRenderable(this->cell_ref, ws, face)){
//       mark_triangle(face.a);
//       mark_triangle(face.b);
//     }
//   }
//
//   return std::count(this->triangleMask.begin(), this->triangleMask.end(), true);
// }
//
// void CellRenderEntity::updateColor(){
//   this->color = ELEMENT_COLOR_LUT[this->cell_ref.cell.elem];
// }
//
// // Written by claude
// void CellRenderEntity::print(){
//   const Cell &cell = this->cell_ref.cell;
//   std::printf("CellRenderEntity at (%d, %d, %d): element=%u state=%u\n",
//               this->cell_ref.coords.x, this->cell_ref.coords.y,
//               this->cell_ref.coords.z, static_cast<unsigned int>(cell.elem),
//               static_cast<unsigned int>(cell.state));
//   std::printf("  geometry=%p\n",
//               static_cast<const void *>(&this->geom));
//   std::printf("  color=(%u, %u, %u, %u) triangles=%zu mask=",
//               static_cast<unsigned int>(this->color.r),
//               static_cast<unsigned int>(this->color.g),
//               static_cast<unsigned int>(this->color.b),
//               static_cast<unsigned int>(this->color.a), this->geom.size());
//   for(bool enabled : this->triangleMask){
//     std::putchar(enabled ? '1' : '0');
//   }
//   std::printf("\n  visible_faces=");
//   static constexpr const char *direction_names[] = {
//       "X_PLUS", "X_MINUS", "Y_PLUS", "Y_MINUS", "Z_PLUS", "Z_MINUS"};
//   bool first_face = true;
//   for(const CellFace &face : CUBE_FACES){
//     const auto is_enabled = [this](TriRef triangle){
//       return triangle < this->geom.size() &&
//              triangle < this->triangleMask.size() &&
//              this->triangleMask[triangle];
//     };
//     if(is_enabled(face.a) || is_enabled(face.b)){
//       if(!first_face) std::printf(", ");
//       std::printf("%s", direction_names[static_cast<unsigned int>(face.d)]);
//       first_face = false;
//     }
//   }
//   if(first_face) std::printf("none");
//   std::putchar('\n');
// }
//
