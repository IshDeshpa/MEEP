#include "camera.h"
#include <cassert>
#include <algorithm>

static void clipVertToNDC(dvec4 &v){
  v /= v.w;
}

static void ndcVertToScreen(dvec4 &v, const Screen &screen){
  v.x = (v.x * 0.5 + 0.5) * screen.getWidth();
  v.y = (v.y * 0.5 + 0.5) * screen.getHeight();
  v.z = v.z * 0.5 + 0.5;
  v.w = 1.0;
}

static double edge_value(const dvec2 &a, const dvec2 &b, const dvec2 &c) {
    return ((c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x));
}

static void rasterize(const vert4Array &screen_space_verts, const tArray &screen_tris, Screen &screen){
  for(unsigned int y=0; y<screen.getHeight(); y++){
    for(unsigned int x=0; x<screen.getWidth(); x++){
      for(const Triangle &t : screen_tris){
        const dvec2 &a = dvec2(screen_space_verts[t.x]);
        const dvec2 &b = dvec2(screen_space_verts[t.y]);
        const dvec2 &c = dvec2(screen_space_verts[t.z]);
        const dvec2 p = dvec2(x, y);

        double e0 = edge_value(a, b, p), e1 = edge_value(b, c, p), e2 = edge_value(c, a, p);
        bool inside = (e0 <= 0 && e1 <= 0 && e2 <= 0) || (e0 >= 0 && e1 >= 0 && e2 >= 0); // both back and front

        if(inside){
          screen[y][x] = {255, 255, 255};
        }
      }
    }
  }
}

void Camera::capture(const vert4Array &world_verts, const tArray &world_tris, const char *out_path){
  screen.clear();
  vert4Array out_verts;
  worldToClip(world_verts, out_verts);
  
  tArray screen_tris;
  // modifies clip space verts, generates new clipped triangles
  clip(out_verts, world_tris, screen_tris);

  // clip to NDC
  std::for_each(out_verts.begin(), out_verts.end(), clipVertToNDC);

  // NDC to screen
  std::for_each(out_verts.begin(), out_verts.end(), [this](dvec4 &v){
    ndcVertToScreen(v, this->screen);
  });

  // rasterize
  rasterize(out_verts, screen_tris, screen); 

  // write out
  screen.writePPM(out_path);
}

void Camera::worldToClip(const vert4Array &world_verts, vert4Array &out){
  out.resize(world_verts.size());

  const dmat4x4 pv = getP() * getV();
  for(unsigned int i=0; i<out.size(); i++){
    out[i] = pv * world_verts[i];
  }
}

// generic transform
void Camera::transform(dmat4x4 &m){
  pose.pos = (m * pose.pos);
  pose.look = normalize(m * pose.look);
}

// Rotation q about pivot: T(pivot) * R * T(-pivot).
// pos is a point (w=1) and look is a direction (w=0), so transform() translates
// only the position while rotating both.
static dmat4x4 rotateAboutMat(const dvec3 &pivot, const dquat &q){
  return glm::translate(dmat4x4(1.0), pivot)
       * mat4_cast(q)
       * glm::translate(dmat4x4(1.0), -pivot);
}

// Orbit about the pivot `pos` around the world up axis.
void Camera::orbit(dvec3 &pos, double deg){
  dmat4x4 m = rotateAboutMat(pos, angleAxis(glm::radians(deg), dvec3(0, 1, 0)));
  transform(m);
}

void Camera::translate(dvec3 &trans){
  dmat4x4 m = glm::translate(dmat4x4(1.0), trans);
  transform(m);
}

// Rotate in place: orbit about the camera's own position, so pos is fixed and look turns.
void Camera::rotate(dquat &q){
  dmat4x4 m = rotateAboutMat(dvec3(pose.pos), q);
  transform(m);
}

// deg = (pitch about x, yaw about y, roll about z) in degrees.
// Roll has no effect on the view since getLookQuat() derives orientation from look with fixed up.
void Camera::rotateDegAxis(dvec3 &deg){
  dquat q = dquat(glm::radians(deg));
  rotate(q);
}
