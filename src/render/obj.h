#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "movable.h"

using namespace glm;

using vert4Array = std::vector<dvec4>;
using Triangle = uvec3; // 3 indices into the vertices arrays
using tArray = std::vector<Triangle>;

struct Pose {
  dvec4 pos = dvec4(0, 0, 0, 1); // position
  dvec4 look = dvec4(0, 0, -1, 0); // look direction

  Pose() = default;
  Pose(const dvec4 &p, const dvec4 &l): pos(p), look(l) {}

  // pos is a point (w=1) and look is a direction (w=0), so this translates
  // only the position while rotating both.
  void apply(const dmat4x4 &m){
    pos = m * pos;
    look = normalize(m * look);
  }

  // orientation derived from look with fixed world up
  dquat lookQuat() const {
    return quatLookAt(normalize(dvec3(look)), dvec3(0, 1, 0));
  }
};

class Mesh{
public:
  Mesh() = default; // empty, nothing loaded
  explicit Mesh(const std::string &path); // loads the OBJ at path
  Mesh(vert4Array v, tArray t): verts(std::move(v)), tris(std::move(t)) {}

  vert4Array &getVerts() { return verts; }
  const vert4Array &getVerts() const { return verts; }
  tArray &getTris() { return tris; }
  const tArray &getTris() const { return tris; }

  // copy of this mesh with every vertex multiplied by m
  Mesh transformed(const dmat4x4 &m) const;

  dvec4 getCentroid();
  Mesh operator+(const Mesh& b) const;

  friend std::istream& operator>>(std::istream& is, Mesh& o);
  friend std::ostream& operator<<(std::ostream& os, const Mesh& o);

private:
  // A set of triangles
  vert4Array verts;
  tArray tris;
  std::string name;
  
  void load(const std::string &path);
  void load(std::istream &is);
};

// Mesh with a Pose
class Obj : public Movable{
public:
  Obj() = default;
  explicit Obj(const Pose &p): pose(p) {}
  explicit Obj(const std::string &path): m(path) {}
  Obj(const std::string &path, const dvec4 &pos, const dvec4 &look): m(path), pose(pos, look) {}

  const Mesh &getMesh() const { return m; }
  const Pose &getPose() const { return pose; }

  // mesh placed in the world by the object's pose
  Mesh toWorld() const;

  void transform(const dmat4x4 &trans) override { pose.apply(trans); }
  dvec3 getPosition() const override { return dvec3(pose.pos); }

protected:
  Mesh m;
  Pose pose;
};
