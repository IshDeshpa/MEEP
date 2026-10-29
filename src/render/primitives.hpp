#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <string>

#include "state/state.hpp"

using namespace glm;

using Vertex=dvec3; // might change later if normals/other are added. heterogeneous coords
using Tri=uvec3; // indices pointing to vertices

using VertRef=Tri::value_type;
using TriRef=unsigned int; // index pointing to a triangle (use instead of a Tri&, since those refs may be invalidated since tris are in vectors)

// LUTS
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

struct Color {
  uint8_t r; uint8_t g; uint8_t b; uint8_t a;
};

constexpr Color ELEMENT_COLOR_LUT[] = {
    {  0,   0,   0,   0}, // AIR
    { 30, 144, 255, 255}, // WATER
    {139,  90,  43, 255}, // EARTH
    {255,  69,   0, 255}, // LAVA
    {100, 100, 110, 255}, // BLOCK
    {255, 215,   0, 255}, // EMITTER
    { 40,  10,  60, 255}, // VOID
};
static_assert(static_cast<int>(Element::COUNT) == std::size(ELEMENT_COLOR_LUT));

template <typename K, typename V>
    requires std::is_enum_v<K>
constexpr V lutGet(const V* lut, K key) noexcept {
    return lut[static_cast<std::size_t>(key)];
}

// A set of triangles and vertices, meant to be passed to the GPU as a batch
struct Geometry{
  std::vector<Vertex> verts;
  std::vector<Tri> tris;

  Geometry(){}

  explicit Geometry(const std::string &path){
    this->load(path);
  }

  VertRef addVertex(Vertex v){
    const VertRef index = static_cast<VertRef>(verts.size());
    verts.push_back(v);
    return index;
  }

  void addTriangle(Tri indices){
    tris.push_back(indices);
  }

  void addTriangle(Vertex a, Vertex b, Vertex c){
    const VertRef ia = addVertex(a);
    const VertRef ib = addVertex(b);
    const VertRef ic = addVertex(c);
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
    for (Tri tri : other.tris) {
        tri[0] += offset;
        tri[1] += offset;
        tri[2] += offset;
        tris.push_back(tri);
    }

    return *this;
  }

  void load(const std::string &path);
  void save(const std::string &path);
  void print();

  std::size_t size() const {return tris.size();}
};

// A collection of triangles in a geometry
// Refers to a range of triangles in some geometry reference
struct Mesh{
  const Geometry &geom;
  TriRef first; // inclusive
  TriRef last; // inclusive

  Mesh(const Geometry &g, TriRef f, TriRef l): geom(g), first(f), last(l){}
};

// Refers to some mesh to identify which triangles are part of this entity
// This is so that multiple entities can share meshes
// Has its own translation/rotation
struct Entity{
  const Mesh &mesh;
  std::vector<bool> meshMask; // mask out specific tris in the mesh; indexed by TriRef

  Color color;
  dmat4 transform;

  Entity(const Mesh &m, Color c, dmat4 t): mesh(m), color(c), transform(t){}
  Entity(const Mesh &m, Color c, dvec3 translation, dvec3 rot_euler): mesh(m), color(c), transform(toTransform(translation, rot_euler)){}

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

