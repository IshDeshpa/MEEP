#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <glm/glm.hpp>

using namespace glm;

using vert4Array = std::vector<dvec4>;
using Triangle = uvec3; // 3 indices into the vertices arrays
using tArray = std::vector<Triangle>;


class Obj{
  // A set of triangles
  vert4Array verts;
  tArray tris;
  std::string name;
  
  void load(const std::string &path);
  void load(std::istream &is);

  friend std::istream& operator>>(std::istream& is, Obj& o);
  friend std::ostream& operator<<(std::ostream& os, const Obj& o);
};
