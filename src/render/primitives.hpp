#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <memory>

using namespace glm;

using Vertex=dvec3; // might change later if normals/other are added. heterogeneous coords
using TriInds=uvec3; // indices pointing to vertices

struct Color {
  uint8_t r; uint8_t g; uint8_t b; uint8_t a;
};

enum class Direction : uint8_t{
  X_PLUS=0,
  X_MINUS,
  Y_PLUS,
  Y_MINUS,
  Z_PLUS,
  Z_MINUS,
  COUNT
};

constexpr vec3 DIRECTION_LUT[] = {
  vec3(1, 0, 0),
  vec3(-1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, -1, 0),
  vec3(0, 0, 1),
  vec3(0, 0, -1),
};
static_assert(static_cast<int>(Direction::COUNT) == std::size(DIRECTION_LUT));

template <typename K, typename V>
    requires std::is_enum_v<K>
constexpr V lutGet(const V* lut, K key) noexcept {
    return lut[static_cast<std::size_t>(key)];
}

struct Face{
  TriInds a; // TL, BR, BL
  TriInds b; // TR, BR, TL
  Direction d; // what side of the cube is it on?
};

// A set of triangles and vertices, meant to be passed to the GPU as a batch
struct Geometry{
  std::vector<Vertex> verts;
  std::vector<TriInds> tris;

  unsigned int addVertex(Vertex v){
    const unsigned int index = static_cast<unsigned int>(verts.size());
    verts.push_back(v);
    return index;
  }

  void addTriangle(TriInds indices){
    tris.push_back(indices);
  }

  void addTriangle(Vertex a, Vertex b, Vertex c){
    const unsigned int ia = addVertex(a);
    const unsigned int ib = addVertex(b);
    const unsigned int ic = addVertex(c);
    addTriangle({ia, ib, ic});
  }

  // To combine two geometries
  Geometry &operator+=(const Geometry &other){
    if(this == &other){
      Geometry copy(other);
      return *this += copy;
    }

    const std::size_t offset = verts.size();
    verts.insert(verts.end(), other.verts.begin(), other.verts.end());

    tris.reserve(tris.size() + other.tris.size());
    for (TriInds tri : other.tris) {
        tri[0] += offset;
        tri[1] += offset;
        tri[2] += offset;
        tris.push_back(tri);
    }

    return *this;
  }

  void load(const std::string &path);
  void save(const std::string &path);
};

// A collection of triangles in a geometry
// Refers to a range of triangles in some geometry reference
struct Mesh{
  Geometry &geom;
  unsigned int first_tri_ind; // inclusive
  unsigned int last_tri_ind; // inclusive

  Mesh(Geometry &g, unsigned int f, unsigned int l): geom(g), first_tri_ind(f), last_tri_ind(l){}
};

// Refers to some mesh to identify which triangles are part of this entity
// This is so that multiple entities can share meshes
// Has its own translation/rotation
struct Entity{
  Mesh &mesh;
  Color color;
  dmat4 transform;

  Entity(Mesh &m, Color c, dmat4 t): mesh(m), color(c), transform(t){}
  Entity(Mesh &m, Color c, dvec3 translation, dvec3 rot_euler): mesh(m), color(c), transform(toTransform(translation, rot_euler)){}

  dmat4 toTransform(dvec3 translation, dvec3 rot_euler) {
      return translate(dmat4(1.0), translation)
          * eulerAngleXYZ(rot_euler.x, rot_euler.y, rot_euler.z);
  }
};

// mainly for camera, and perhaps for other objects that have a look direction and position
// (thinking light source maybe?)
struct Pose {
  dvec4 pos = dvec4(0, 0, 0, 1); // position
  dvec4 look = dvec4(0, 0, -1, 0); // look direction
};

