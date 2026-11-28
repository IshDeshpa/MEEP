#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <glm/geometric.hpp>
#include <glm/fwd.hpp>
#include "render/camera.hpp"
#include "extra/check.hpp"
#include "state/state.hpp"
#include "render/render.hpp"

int main() {
    [[maybe_unused]] Renderer &r = Renderer::instance();
    WorldState world(10, 10, 10);

    // Seed a few visible cells in the padded CPU initialization image.
    world(Coord(0, 3, 0)) = Cell{Element::AIR, 0};
    world(Coord(1, 3, 0)) = Cell{Element::WATER, 0};
    world(Coord(2, 3, 0)) = Cell{Element::EARTH, 0};
    world(Coord(3, 3, 0)) = Cell{Element::LAVA, 0};
    world(Coord(0, 2, 0)) = Cell{Element::NATURE, 0};
    world(Coord(1, 2, 0)) = Cell{Element::STEAM, 0};
    world(Coord(2, 2, 0)) = Cell{Element::FIRE, 0};
    world(Coord(3, 2, 0)) = Cell{Element::ACID, 0};
    world(Coord(0, 1, 0)) = Cell{Element::ICE, 0};
    world(Coord(1, 1, 0)) = Cell{Element::HOTAIR, 0};
    world(Coord(2, 1, 0)) = Cell{Element::EXPLOSION, 0};
    world(Coord(3, 1, 0)) = Cell{Element::SAND, 0};
    world(Coord(0, 0, 1)) = Cell{Element::BOMB, 0};
    world(Coord(1, 0, 1)) = Cell{Element::BLOCK, 0};
    world(Coord(2, 0, 1)) = Cell{Element::EMITTER, 0};
    world(Coord(3, 0, 1)) = Cell{Element::VOID, 0};

    const auto bytes = world.n_pad * world.m_pad * world.k_pad * sizeof(Cell);
    CUDA_CHECK(cudaMemcpy(world.d_curr, world.world, bytes, cudaMemcpyHostToDevice));

    Camera& camera = Camera::instance();
    const glm::dvec3 pos(-3.0, 3.0, 5.0);
    const glm::dvec3 target(1.5, 1.5, 0.0);

    camera.setPose(pos, glm::normalize(target - pos));

    stateToWorldLaunch(world);
    worldToClipLaunch();
    clipVertsLaunch();
    clipToNDCLaunch();
    ndcToScreenLaunch();
    rasterizeLaunch();
}
