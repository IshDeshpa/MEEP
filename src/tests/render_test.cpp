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

#include "state/state.hpp"
#include "config.hpp"
#include "render/render.hpp"

WorldStateBuf2<WORLD_SIZE_N, WORLD_SIZE_M, WORLD_SIZE_K> world;
FaceETileLUT et_lut;

int main(){
  helloWorld();
}
