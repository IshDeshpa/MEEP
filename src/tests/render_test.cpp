// Plan:
// 1. State starts resident on GPU
// 2. State -> World-space vertices, triangles, and colors
//    - For each cell, determine if it's in the FOV and if it is renderable; generate the vertices it needs to render
// 3. World -> Clip space 
//    - For each vertex, transform to near/far plane box with homogeneous coords
// 4. Run clipping on near plane
//    - For each triangle, see if it is clipped by near plane
// 5. Clipped -> NDC 
//    - 
// 6. NDC -> Screen
// 7. Rasterize screen (assigning colors to pixels appropriately)
// 8. Profit
// 9. Spend profits on frivolous ventures

#include "render/render.hpp"

int main(){
  helloWorld();

  WorldState *w = new WorldState(100, 100, 100);
  
  // Initialize a few cells
  (*w)(0, 0, 0) = {Element::WATER, 0};
  (*w)(1, 0, 0) = {Element::EARTH, 0};
  (*w)(2, 0, 0) = {Element::FIRE, 0};
  (*w)(3, 0, 0) = {Element::AIR, 0};

  

}
