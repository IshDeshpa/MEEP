#include <iostream>
#include <fstream>
#include <string>

#include <camera.h>
#include <obj.h>

// 3 triangles, 3 verts each
static Vert4Array triVerts(){
  return {
    // triangle 0: in front, centered
    {-1.0, -1.0, 0.0, 1.0},
    { 1.0, -1.0, 0.0, 1.0},
    { 0.0,  1.0, 0.0, 1.0},
    // triangle 1: farther back, offset right
    { 0.5, -0.5, -3.0, 1.0},
    { 3.0, -0.5, -3.0, 1.0},
    { 1.75, 2.0, -3.0, 1.0},
    // triangle 2: closer, offset left
    {-3.0, -1.5, 2.0, 1.0},
    {-1.5, -1.5, 2.0, 1.0},
    {-2.25, 0.0, 2.0, 1.0},
  };
}

void testmain1(){
  Vert4Array verts = triVerts();

  // counter-clockwise winding
  TArray tris = {
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8},
  };

  Mesh mesh(verts, tris);

  // center of mass of the (equal-weight) vertices
  dvec3 com(0.0);
  for(const dvec4 &v : verts) com += dvec3(v);
  com /= double(verts.size());

  // Start 5 units in front of the center, looking straight at it (-Z).
  // Orbiting about the center rotates position and look together, so the
  // camera keeps facing the center the whole way around.
  Camera cam(
    0.1,
    100.0,
    60.0,
    dvec4(com + dvec3(0.0, 0.0, 5.0), 1.0),
    dvec4(0.0, 0.0, -1.0, 0.0),
    800,
    800);

  for(int n = 0; n < 360; n++){
    cam.capture(mesh, "out/test" + std::to_string(n) + ".ppm");
    cam.orbit(com, 1.0);
  }
  std::cout << "wrote test0.ppm .. test359.ppm" << std::endl;
}

void testmain2(){
  const Vert4Array verts = triVerts();

  // one copy of the obj at the centroid of each triangle, all looking along `look`
  auto build_scene = [&](const dvec4 &look){
    Mesh global;
    for(unsigned int i = 0; i < verts.size(); i += 3){
      const dvec4 pos = (verts[i] + verts[i + 1] + verts[i + 2]) / 3.0;
      Obj o("obj/test.obj", dvec4(dvec3(pos), 1.0), look, Color(194, 97, 120));
      global = global + o.toWorld();
    }
    return global;
  };

  // every cardinal look direction: +/-X, +/-Y, +/-Z
  const std::vector<std::pair<std::string, dvec4>> looks = {
    {"+x", { 1.0,  0.0,  0.0, 0.0}},
    {"-x", {-1.0,  0.0,  0.0, 0.0}},
    {"+y", { 0.0,  1.0,  0.0, 0.0}},
    {"-y", { 0.0, -1.0,  0.0, 0.0}},
    {"+z", { 0.0,  0.0,  1.0, 0.0}},
    {"-z", { 0.0,  0.0, -1.0, 0.0}},
  };

  std::vector<Mesh> scenes;
  for(const auto &l : looks) scenes.push_back(build_scene(l.second));

  // frame all of the scenes with one camera: center on the first, back off far enough
  // that the bounding sphere of the largest one fits in the view
  const dvec3 com = scenes[0].getCentroid();
  double radius = 0.0;
  for(const Mesh &s : scenes){
    for(const dvec4 &v : s.getVerts()) radius = std::max(radius, length(dvec3(v) - com));
  }

  const double fov_deg = 60.0;
  const double dist = 1.1 * radius / std::sin(radians(fov_deg) / 2.0);

  Camera cam(
    0.1,
    3.0 * dist,
    fov_deg,
    dvec4(com + dvec3(0.0, 0.0, dist), 1.0),
    dvec4(0.0, 0.0, -1.0, 0.0),
    800,
    800);

  for(unsigned int n = 0; n < looks.size(); n++){
    cam.capture(scenes[n], "out/look" + looks[n].first + ".ppm");
  }
  std::cout << "wrote out/look+x.ppm .. out/look-z.ppm" << std::endl;
}

void testmain3(){
  const Vert4Array verts = triVerts();

  // one cube at the centroid of each triangle, each its own color
  const std::vector<Color> colors = {Color(194, 97, 120), Color(97, 194, 120), Color(97, 120, 194)};
  Mesh scene;
  dvec3 com(0.0);
  for(unsigned int i = 0; i < verts.size(); i += 3){
    const dvec4 pos = (verts[i] + verts[i + 1] + verts[i + 2]) / 3.0;
    Obj o("obj/cell.obj", dvec4(dvec3(pos), 1.0), dvec4(0.0, 0.0, -1.0, 0.0), colors[i / 3]);
    scene = scene + o.toWorld();
    com += dvec3(pos);
  }
  com /= double(verts.size() / 3);

  // same camera and orbit as testmain1: start in front of the center and
  // orbit it so the camera keeps facing the center the whole way around
  Camera cam(
    0.1,
    100.0,
    60.0,
    dvec4(com + dvec3(0.0, 0.0, 5.0), 1.0),
    dvec4(0.0, 0.0, -1.0, 0.0),
    800,
    800);

  for(int n = 0; n < 360; n++){
    cam.capture(scene, "out/cubes" + std::to_string(n) + ".ppm");
    cam.orbit(com, 1.0);
  }
  std::cout << "wrote out/cubes0.ppm .. out/cubes359.ppm" << std::endl;
}

int main(){
  //testmain1();
  testmain2();
  //testmain3();
}
