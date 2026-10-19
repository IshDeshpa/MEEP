#pragma once

#include <cstdio>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <stdint.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <obj.h>

using namespace glm;

class Screen {
public:
  struct Pixel {
    uint8_t a,b,c;
  };

  Screen(): WIDTH(800), HEIGHT(800), framebuffer(800 * 800, {0, 0, 0}){}

  Screen(unsigned width, unsigned height): 
    WIDTH(width), HEIGHT(height), framebuffer(width * height, {0,0,0}){}

  unsigned getWidth() const {return WIDTH;}
  unsigned getHeight() const {return HEIGHT;}
  double getAspectRatio() const {return double(WIDTH)/HEIGHT;}

  // screen[y][x] -> Pixel&  (row-major, y is the row)
  Pixel* operator[](unsigned y) { return &framebuffer[size_t(y) * WIDTH]; }
  const Pixel* operator[](unsigned y) const { return &framebuffer[size_t(y) * WIDTH]; }

  void clear(){ 
    std::fill(framebuffer.begin(), framebuffer.end(), Pixel{0, 0, 0}); 
    std::fill(depthbuffer.begin(), depthbuffer.end(), 1.0);
  }

  void writePPM(const std::string &path){
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for(int y=HEIGHT-1; y >= 0; y--){
      std::fwrite(&framebuffer[y*WIDTH], sizeof(Pixel), WIDTH, f);
    }
    std::fclose(f);
  }

private:
  const unsigned WIDTH; // pixels
  const unsigned HEIGHT; // pixels
  std::vector<Pixel> framebuffer;
  std::vector<double> depthbuffer;
};

class Camera {
public:
  struct CameraPose {
    dvec4 pos; // position
    dvec4 look; // look direction
  };

  Camera(): Camera(0.1, 100, 34, dvec4(0, 0, 0, 1), dvec4(0, 0, -1, 0), 800, 800) {}

  Camera(double z_near, double z_far, double fov_deg, dvec4 init_pos, dvec4 init_look, unsigned screen_width, unsigned screen_height):
      pose(init_pos, init_look),
      Z_NEAR(z_near),
      Z_FAR(z_far),
      FOV_DEG(fov_deg),
      screen(screen_width, screen_height),
      A(-(z_far + z_near) / (z_far - z_near)),
      B(-(2 * z_far * z_near) / (z_far - z_near)),
      S(1.0 / std::tan(radians(fov_deg) / 2.0)),
      P(makeP(A, B, S, double(screen_width) / screen_height))
  {
    assert(init_pos[3] == 1.0);
    assert(init_look[3] == 0.0);
  }

  const dmat4x4 &getP() const { return P; }

  const dmat4x4 getV() const {
    const dmat4x4 r = mat4_cast(conjugate(getLookQuat()));
    const dmat4x4 t = glm::translate(dmat4x4(1.0), -dvec3(pose.pos));
    return r * t;
  }

  void capture(const vert4Array &world_verts, const tArray &world_tris, const std::string &out_path);

  void transform(dmat4x4 &trans);
  void orbit(dvec3 &pos, double deg);
  void translate(dvec3 &trans);
  void rotate(dquat &q);
  void rotateDegAxis(dvec3 &deg);

private:
  // May vary
  CameraPose pose;
  
  // Fixed at construction (think like camera's inherent parameters)
  const double Z_NEAR;
  const double Z_FAR;
  const double FOV_DEG;
  Screen screen;

  // For projection (declaration order matters: P is built from A, B, S)
  const double A, B, S;
  const dmat4x4 P;

  static dmat4x4 makeP(double a, double b, double s, double aspect){
    return {
      s / aspect, 0, 0, 0,
      0, s, 0, 0,
      0, 0, a, -1,
      0, 0, b, 0
    };
  }

  const dquat getLookQuat() const {
    return quatLookAt(
      normalize(
        dvec3(pose.look)
      ), 
      dvec3(0,1,0)
    );
  }

  // space -> space
  void worldToClip(const vert4Array &world_verts, vert4Array &out);
  void clip(vert4Array &clip_space_verts, const tArray &tris, tArray &out_tris);

};

