#include <cuda_runtime_api.h>
#include "state/state.hpp"
#include "render/render.hpp"

int main() {
    [[maybe_unused]] Renderer &r = Renderer::instance();
    WorldState world(10, 10, 10);

    // Seed a few visible cells in the padded CPU initialization image.
    world.device_view.writeCell(worldcoord(0, 3, 0), Cell{Element::AIR, 0});
    world.device_view.writeCell(worldcoord(1, 3, 0), Cell{Element::WATER, 0});
    world.device_view.writeCell(worldcoord(2, 3, 0), Cell{Element::EARTH, 0});
    world.device_view.writeCell(worldcoord(3, 3, 0), Cell{Element::LAVA, 0});
    world.device_view.writeCell(worldcoord(0, 2, 0), Cell{Element::NATURE, 0});
    world.device_view.writeCell(worldcoord(1, 2, 0), Cell{Element::STEAM, 0});
    world.device_view.writeCell(worldcoord(2, 2, 0), Cell{Element::FIRE, 0});
    world.device_view.writeCell(worldcoord(3, 2, 0), Cell{Element::ACID, 0});
    world.device_view.writeCell(worldcoord(0, 1, 0), Cell{Element::ICE, 0});
    world.device_view.writeCell(worldcoord(1, 1, 0), Cell{Element::HOTAIR, 0});
    world.device_view.writeCell(worldcoord(2, 1, 0), Cell{Element::EXPLOSION, 0});
    world.device_view.writeCell(worldcoord(3, 1, 0), Cell{Element::SAND, 0});
    world.device_view.writeCell(worldcoord(0, 0, 0), Cell{Element::BOMB, 0});
    world.device_view.writeCell(worldcoord(1, 0, 0), Cell{Element::BLOCK, 0});
    world.device_view.writeCell(worldcoord(2, 0, 0), Cell{Element::EMITTER, 0});
    world.device_view.writeCell(worldcoord(3, 0, 0), Cell{Element::VOID, 0});

    // Update the world s.t. we switch buffers; now curr is next
    world.swapBuffers(); // now device_view is updated, and that's what'll be passed to stateToWorldLaunch

    Camera& camera = Camera::instance();
    const glm::dvec3 pos(1.0, 3.0, 4.5);
    const glm::dvec3 target(1.5, 1.5, 0.0);
    const glm::dvec3 up(0.0, 1.0, 0.0);

    camera.pose = glm::inverse(glm::lookAt(pos, target, up));

    stateToWorldLaunch(world);
    worldToClipLaunch();
    clipVertsLaunch();
    clipToNDCLaunch();
    ndcToScreenLaunch();
    rasterizeLaunch();
}
