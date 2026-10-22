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

static void rasterize(const Vert4Array &screen_space_verts, const TArray &screen_tris, Screen &screen, const std::vector<Color> &colors){
  for(unsigned int y=0; y<screen.getHeight(); y++){
    for(unsigned int x=0; x<screen.getWidth(); x++){
      for(unsigned int i=0; i<screen_tris.size(); i++){
        const Triangle &t = screen_tris[i];
        const dvec2 &a = dvec2(screen_space_verts[t.x]);
        const dvec2 &b = dvec2(screen_space_verts[t.y]);
        const dvec2 &c = dvec2(screen_space_verts[t.z]);
        const dvec2 p = dvec2(x, y);

        double e0 = edge_value(a, b, p), e1 = edge_value(b, c, p), e2 = edge_value(c, a, p);
        bool inside = (e0 <= 0 && e1 <= 0 && e2 <= 0) || (e0 >= 0 && e1 >= 0 && e2 >= 0); // both back and front

        if(inside){
          screen[y][x] = colors[i];
        }
      }
    }
  }
}

void Camera::capture(const Mesh &world_mesh, const std::string &out_path){
  screen.clear();
  Mesh clip_mesh = worldToClip(world_mesh);

  // appends new verts to the clip space mesh, replaces its triangles with the clipped ones
  clip(clip_mesh);
  Vert4Array &out_verts = clip_mesh.getVerts();

  // clip to NDC
  std::for_each(out_verts.begin(), out_verts.end(), clipVertToNDC);

  // NDC to screen
  std::for_each(out_verts.begin(), out_verts.end(), [this](dvec4 &v){
    ndcVertToScreen(v, this->screen);
  });

  // rasterize
  rasterize(out_verts, clip_mesh.getTris(), screen, clip_mesh.getColors()); 

  // write out
  screen.writePPM(out_path);
}

Mesh Camera::worldToClip(const Mesh &world_mesh){
  return world_mesh.transformed(getP() * getV());
}
