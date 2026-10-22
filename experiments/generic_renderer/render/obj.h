#pragma once

#include <cmath>
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "movable.h"

using namespace glm;

using Vert4Array = std::vector<dvec4>;
using Triangle = uvec3; // 3 indices into the vertices arrays
using Color = u8vec3; // 8-bit rgb
using TArray = std::vector<Triangle>;

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
    const dvec3 dir = normalize(dvec3(look));
    // looking straight up/down is parallel to world up, which leaves roll undetermined,
    // so pick z as the up reference (same as pitching +/-90 degrees about x)
    const dvec3 up = std::abs(dir.y) > 1.0 - 1e-9 ? dvec3(0, 0, dir.y > 0 ? 1 : -1) : dvec3(0, 1, 0);
    return quatLookAt(dir, up);
  }
};

class Mesh{
public:
  Mesh() = default; // empty, nothing loaded
  explicit Mesh(const std::string &path); // loads the OBJ at path
  
  // loads the OBJ at path and colors every triangle c
  Mesh(const std::string &path, const Color &c): Mesh(path) { colors.assign(tris.size(), c); }
  Mesh(Vert4Array v, TArray t, const Color &c = Color(255, 255, 255)): verts(std::move(v)), tris(std::move(t)) { colors.assign(tris.size(), c); }

  Vert4Array &getVerts() { return verts; }
  const Vert4Array &getVerts() const { return verts; }
  TArray &getTris() { return tris; }
  const TArray &getTris() const { return tris; }
  std::vector<Color> &getColors(){return colors;}
  const std::vector<Color> &getColors() const {return colors;}

  // copy of this mesh with every vertex multiplied by m
  Mesh transformed(const dmat4x4 &m) const;

  dvec4 getCentroid();
  Mesh operator+(const Mesh& b) const;

  friend std::istream& operator>>(std::istream& is, Mesh& o);
  friend std::ostream& operator<<(std::ostream& os, const Mesh& o);

private:
  // A set of triangles
  Vert4Array verts;
  TArray tris;
  std::vector<Color> colors;
  std::string name;
  
  void load(const std::string &path);
  void load(std::istream &is);
};

// Mesh with a Pose
class Obj : public Movable{
public:
  Obj() = default;
  explicit Obj(const Pose &p): pose(p) {}
  explicit Obj(const std::string &path): mesh(path) {}
  Obj(const std::string &path, const dvec4 &pos, const dvec4 &look, const Color &color): mesh(path, color), pose(pos, look), color(color) {}
  Obj(const std::string &path, const dvec4 &pos, const dvec4 &look): Obj(path, pos, look, Color(255, 255, 255)) {}

  const Mesh &getMesh() const { return mesh; }
  const Pose &getPose() const { return pose; }
  const Color &getColor() const {return color;}

  // mesh placed in the world by the object's pose
  Mesh toWorld() const;

  void transform(const dmat4x4 &trans) override { pose.apply(trans); }
  dvec3 getPosition() const override { return dvec3(pose.pos); }

protected:
  Mesh mesh;
  Pose pose;
  Color color = Color(255, 255, 255);
};
