// Plan:
// 1. State is resident on GPU
// 2. State -> World-space vertices, triangles, and colors
// 3. World -> Clip space 
// 4. Run clipping on near plane
// 5. Clipped -> NDC 
// 6. NDC -> Screen
// 7. Rasterize screen (assigning colors to pixels appropriately)
// 8. Profit
// 9. Spend profits on frivolous ventures

#include "render/render.hpp"

int main(){
  helloWorld();
}
