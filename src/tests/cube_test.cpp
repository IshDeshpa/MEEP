#include "render/primitives.hpp"

int main(int argc, char *argv[]){
  if(argc < 2) exit(-1);

  Geometry cube;
  if(std::string(argv[1]) == "construct"){
    const unsigned int a = cube.addVertex({-0.5, -0.5, -0.5});
    const unsigned int b = cube.addVertex({ 0.5, -0.5, -0.5});
    const unsigned int c = cube.addVertex({ 0.5,  0.5, -0.5});
    const unsigned int d = cube.addVertex({-0.5,  0.5, -0.5});
    const unsigned int e = cube.addVertex({-0.5, -0.5,  0.5});
    const unsigned int f = cube.addVertex({ 0.5, -0.5,  0.5});
    const unsigned int g = cube.addVertex({ 0.5,  0.5,  0.5});
    const unsigned int h = cube.addVertex({-0.5,  0.5,  0.5});

    // Counterclockwise when viewed from outside each face.
    cube.addTriangle({a, d, c}); // -Z
    cube.addTriangle({a, c, b});
    cube.addTriangle({e, f, g}); // +Z
    cube.addTriangle({e, g, h});
    cube.addTriangle({a, e, h}); // -X
    cube.addTriangle({a, h, d});
    cube.addTriangle({b, c, g}); // +X
    cube.addTriangle({b, g, f});
    cube.addTriangle({a, b, f}); // -Y
    cube.addTriangle({a, f, e});
    cube.addTriangle({d, h, g}); // +Y
    cube.addTriangle({d, g, c});

    cube.save("./assets/cube.obj");
  } else if(std::string(argv[1]) == "load"){
    cube.load("./assets/cube.obj");
    cube.print();
  }
}
