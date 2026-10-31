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

#include <cuda_runtime_api.h>

#include "extra/check.hpp"
#include "render/cell.hpp"
#include "render/render.hpp"
#include "state/state.hpp"

#include <cstdio>
#include <memory>
#include <vector>

int main() {
    helloWorld();

    WorldState world(100, 100, 100);

    // Seed a few visible cells in the padded CPU initialization image.
    world(worldcoord(0, 0, 0)) = Cell{Element::WATER, 0};
    world(worldcoord(1, 0, 0)) = Cell{Element::EARTH, 0};
    world(worldcoord(2, 0, 0)) = Cell{Element::LAVA, 0};

    // Upload the CPU image, then obtain the renderer's snapshot through the
    // current GPU buffer described by DeviceWorldView.
    const std::size_t padded_cell_count =
        world.n_pad * world.m_pad * world.k_pad;
    const std::size_t padded_bytes = padded_cell_count * sizeof(Cell);
    CUDA_CHECK(cudaMemcpy(world.d_curr, world.world, padded_bytes, cudaMemcpyHostToDevice));

    const DeviceWorldView device_world = world.device_view;
    std::vector<Cell> snapshot(padded_cell_count);
    CUDA_CHECK(cudaMemcpy(snapshot.data(), device_world.curr, padded_bytes, cudaMemcpyDeviceToHost));

    // CellRenderEntity holds references, so keep the snapshot and refs alive
    // until all entities have been printed.
    std::vector<CellRef> refs;
    for (dim x = 0; x < world.n; ++x) {
        for (dim y = 0; y < world.m; ++y) {
            for (dim z = 0; z < world.k; ++z) {
                const worldcoord coords(static_cast<int>(x), static_cast<int>(y),
                                   static_cast<int>(z));
                Cell& cell = snapshot[device_world.index(x, y, z)];
                if (cell.elem != Element::AIR) {
                    refs.emplace_back(cell, coords);
                }
            }
        }
    }

    std::vector<std::unique_ptr<CellRenderEntity>> entities;
    for (CellRef& ref : refs) {
        std::unique_ptr<CellRenderEntity> entity = CellRenderEntity::create(ref, world);
        if (entity) {
            entity->updateTriangleMask(world);
            entities.push_back(std::move(entity));
        }
    }

    for (const auto& entity : entities) {
        entity->print();
    }
}
