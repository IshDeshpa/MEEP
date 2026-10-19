#include <iostream>
#include <fstream>
#include <string>

#include <camera.h>
#include <obj.h>

void testmain1(){
  vert4Array verts = {
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

  // counter-clockwise winding
  tArray tris = {
    {0, 1, 2},
    {3, 4, 5},
    {6, 7, 8},
  };

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
    cam.capture(verts, tris, "out/test" + std::to_string(n) + ".ppm");
    cam.orbit(com, 1.0);
  }
  std::cout << "wrote test0.ppm .. test359.ppm" << std::endl;
}

void testmain2(){
  std::ifstream f("obj/test.obj");
  Obj o;
  f >> o;

  std::ofstream of("obj/test_clone.obj");
  of << o;
}

int main(){
  //testmain1();
  testmain2();
}
