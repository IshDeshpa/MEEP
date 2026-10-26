#include "state/state.hpp"
#include "render/render.hpp"
#include "render/camera.hpp"

#include <cstdio>
#include <stddef.h>

__global__ void helloWorldKernel(){
  printf("Hello World!\n");
}

void helloWorld(){
  helloWorldKernel<<<1,1>>>();
  cudaDeviceSynchronize();
}


// Conditions for being able to render a block:
// 1) inside view of camera
// 2) not AIR
bool isRenderable(Cell &c){
  return (c.elem != Element::AIR);
}

// Generate the list of triangles that need to be rendered
CellRenderEntity *genRenderFaces(Cell &c, Camera &cam){
  if(!isRenderable(c)) return NULL;
  
  
  // Conditions for being able to render a face:
  // 1) shared faces are usually non-renderable, except for sharing with AIR (which really means it's a non-shared face)
  

  // To allocate full world triangle vector, we must identify all triangles 
  // that are necessary to render, and size the vector to that count.

}

bool isFaceShared(
  dim_t x1, dim_t y1, dim_t z1, // Cell 1 coords
  dim_t x2, dim_t y2, dim_t z2, // Cell 2 coords
  WorldState &ws){

}

