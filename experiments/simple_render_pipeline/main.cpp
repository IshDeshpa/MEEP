#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

constexpr int WIDTH  = 800;
constexpr int HEIGHT = 800;
constexpr double ASPECT = double(WIDTH) / HEIGHT;

// vertex and triangle
using world_vertex=glm::dvec4;
struct world_triangle_indices {
  unsigned a,b,c;
};

struct cam_pose {
  glm::dvec4 pos;
  glm::dvec4 look;
};

constexpr double Z_NEAR = 0.1;
constexpr double Z_FAR = 100;

// world coordinates (right now)
std::vector<world_vertex> vertices = {
  {3, 1, 5, 1},
  {7, 1, 5, 1},
  {3, 5, 5, 1}
};

std::vector<world_triangle_indices> triangles = {
{0, 1, 2}
};

// output screen coords
using screen_coord = glm::dvec3;
struct screen_triangle_coords {
  glm::dvec3 a,b,c;
};

std::vector<screen_coord> scs;
std::vector<screen_triangle_coords> screen_triangles;

// framebuffer
struct rgb {uint8_t r,g,b;}; // 3 bytes per pix
std::vector<rgb> framebuffer(WIDTH * HEIGHT, {0,0,0});

constexpr double FOV_DEG = 34;
constexpr double FOV_RAD = FOV_DEG * (M_PI / 180.0);

// vertex shader: runs over vertices (later)
// rasterize
//  1. project vertices from 3d space onto 2d camera plane
//  2. loop over pixels; determine if the object is on or off
// fragment shader: runs over pixels (later)

// View Matrix
glm::dvec4 camera_pos = {5, 3, -5, 1};
glm::dvec4 camera_look_vec = {0, 0, 1, 0};
glm::dquat camera_orientation = glm::quatLookAt(
    glm::normalize(glm::dvec3(camera_look_vec)),
    glm::dvec3(0, 1, 0)
);

glm::dmat4x4 R = glm::mat4_cast(glm::conjugate(camera_orientation)); // rotate by camera orientation so that camera looks in -Z direction
glm::dmat4x4 T = glm::translate(glm::dmat4x4(1.0), -glm::dvec3(camera_pos)); // translate to camera-relative coordinates where camera is at center
glm::dmat4x4 V = R * T; // translate, then rotate

// Projection Matrix
// projection matrix (https://www.scratchapixel.com/lessons/3d-basic-rendering/perspective-and-orthographic-projection-matrix//building-basic-perspective-projection-matrix.html) for camera -> clip space
// in the camera coords, camera points in -Z direction
constexpr double A = (-(Z_FAR+Z_NEAR)/(Z_FAR - Z_NEAR));
constexpr double B = (-(2*Z_FAR*Z_NEAR)/(Z_FAR - Z_NEAR));
const double S = (1.0 / std::tan(FOV_RAD/2.0));
const glm::dmat4x4 P = {
S / ASPECT, 0, 0, 0,
0, S, 0, 0,
0, 0, A, -1,
0, 0, B, 0
};

// Clip
bool clip(glm::dvec4 &v){
  const double w = v.w;
  if(w <= 0 ||
      v.x < -w || v.x > w ||
      v.y < -w || v.y > w ||
      v.z < -w || v.z > w){
    return true;
  }
  return false;
}


// Clip to NDC
glm::dvec3 toNDC(glm::dvec4 &v){
  return glm::dvec3(v/v.w);
}

// NDC to screen
glm::dvec3 toScreen(glm::dvec3 &v){
  return glm::dvec3{
    (v.x * 0.5 + 0.5) * WIDTH,
    (v.y * 0.5 + 0.5) * HEIGHT,
    v.z * 0.5 + 0.5
  };
}

bool edge_function(const glm::dvec2 &a, const glm::dvec2 &b, const glm::dvec2 &c) {
    return ((c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x) >= 0);
}

void write_ppm(const char *path){
  FILE *f = std::fopen(path, "wb");
  std::fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
  for(int y=HEIGHT-1; y >= 0; y--){
    std::fwrite(&framebuffer[y*WIDTH], sizeof(rgb), WIDTH, f);
  }
  std::fclose(f);
}

int main(){
  for(world_vertex v : vertices){
    std::printf("World: %.2f, %.2f, %.2f, %.2f \n", v.x, v.y, v.z, v.w);

    glm::dvec4 cam_coords = V*v;
    std::printf("Camera: %.2f, %.2f, %.2f, %.2f \n", cam_coords.x, cam_coords.y, cam_coords.z, cam_coords.w);

    glm::dvec4 clip_coords = P*cam_coords;
    std::printf("Clip: %.2f, %.2f, %.2f, %.2f \n", clip_coords.x, clip_coords.y, clip_coords.z, clip_coords.w);

    // reject vertices outside the clip volume (w <= 0 means at/behind the camera)
    if(clip(clip_coords)){
      std::printf("\n");
      scs.push_back({-1, -1, -1});
      continue;
    }

    glm::dvec3 NDC_coords = toNDC(clip_coords);
    std::printf("NDC: %.2f, %.2f, %.2f \n", NDC_coords.x, NDC_coords.y, NDC_coords.z);

    glm::dvec3 screen_coords = toScreen(NDC_coords);
    std::printf("Screen: %.2f, %.2f, %.2f \n", screen_coords.x, screen_coords.y, screen_coords.z);

    scs.push_back(screen_coords);

    std::printf("\n");
  }

  for(world_triangle_indices t : triangles){
    screen_triangle_coords stc = {
      scs[t.a],
      scs[t.b],
      scs[t.c]
    };

    screen_triangles.push_back(stc); 
    printf("Screen triangle: \n\t<%.2f, %.2f, %.2f>\n\t<%.2f, %.2f, %.2f>\n\t<%.2f, %.2f, %.2f>", 
           stc.a.x, stc.a.y, stc.a.z,
           stc.b.x, stc.b.y, stc.b.z,
           stc.c.x, stc.c.y, stc.c.z
    );
  }

  printf("\n");

  for(int y=0; y<HEIGHT; y++){
    for(int x=0; x<WIDTH; x++){
      for(screen_triangle_coords stc : screen_triangles){
        bool inside = edge_function(stc.a, stc.b, glm::dvec2(x, y)) &&
                      edge_function(stc.b, stc.c, glm::dvec2(x, y)) &&
                      edge_function(stc.c, stc.a, glm::dvec2(x, y));

        if(inside) framebuffer[y * WIDTH + x] = {255, 255, 255};
      }
    }
  }

  write_ppm("./test.ppm");
}

