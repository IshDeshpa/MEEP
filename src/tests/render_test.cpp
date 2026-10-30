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

#include "render/cell.hpp"
#include "state/state.hpp"
#include "render/render.hpp"

#include <memory>
#include <utility>
#include <vector>

int main(){
  helloWorld();

  WorldState *w = new WorldState(100, 100, 100);

  // Initialize a few cells
  (*w)(0, 0, 0) = Cell{Element::WATER, 0};
  (*w)(1, 0, 0) = Cell{Element::EARTH, 0};
  (*w)(2, 0, 0) = Cell{Element::LAVA, 0};
  (*w)(3, 0, 0) = Cell{Element::AIR, 0};

  // Set up CellRefs
  std::vector<CellRef *> refs = std::vector<CellRef *>();
  for(unsigned int x=0; x<w->n(); x++){
    for(unsigned int y=0; y<w->m(); y++){
      for(unsigned int z=0; z<w->k(); z++){
        refs.push_back(new CellRef((*w)(x,y,z), {x, y, z}));
      }
    }
  }

  // Set up CellRenderEntity per cell, using the factory function
  std::vector<std::unique_ptr<CellRenderEntity>> entities = std::vector<std::unique_ptr<CellRenderEntity>>();

  for(CellRef *r : refs){
    std::unique_ptr<CellRenderEntity> cre = CellRenderEntity::create(*r, *w);
    if(cre != nullptr) {
      entities.push_back(std::move(cre));
      cre->updateMeshMask(*w);
    }
  }

  // Print the CellRenderEntities
  for(std::unique_ptr<CellRenderEntity> &cre : entities){
    cre->print();
  }

  // Transfer state to GPU (d_curr)
  w->transferWorld();
  
}
