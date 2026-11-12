#include <cuda_runtime_api.h>
#include "state/state.hpp"
#include "render/render.hpp"

int main() {
    [[maybe_unused]] Renderer &r = Renderer::instance();
    WorldState world(10, 10, 10);

    // Seed a few visible cells in the padded CPU initialization image.
    world.device_view.writeCell(worldcoord(0, 0, 0), Cell{Element::WATER, 0});
    world.device_view.writeCell(worldcoord(1, 0, 0), Cell{Element::EARTH, 0});
    world.device_view.writeCell(worldcoord(2, 0, 0), Cell{Element::LAVA, 0});

    // Update the world s.t. we switch buffers; now curr is next
    world.swapBuffers(); // now device_view is updated, and that's what'll be passed to stateToWorldLaunch

    stateToWorldLaunch(world);
    worldToClipLaunch();
    clipVertsLaunch();
}
