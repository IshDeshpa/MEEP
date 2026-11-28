#include "render/render.hpp"
#include "render/window.hpp"
#include "tick/tick.hpp"
#include "config.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

void renderTick(std::stop_token stop_token, GameWindow& window,
                Renderer&, Camera& camera, WorldState& world,
                std::mutex& world_mutex, TickSource& render_tick) {
    while (!stop_token.stop_requested()) {
      if (RENDER_TICK_RATE <= 0.0F) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      time_point start = Clock::now();

      bool has_entities;
      {
        std::lock_guard lock(world_mutex);
        // This call waits for all world reads on render_stream before returning.
        has_entities = stateToWorldLaunch(world);
      }

      if (has_entities) {
        worldToClipLaunch();
        clipVertsLaunch();
        clipToNDCLaunch();
        ndcToScreenLaunch();
        rasterizeLaunch();
      } else {
        std::fill(camera.framebuffer.begin(), camera.framebuffer.end(), Color{50, 50, 50, 255});
      }

      // update window
      window.updateScreen(camera);

#ifdef MEEP_DEBUG
      // camera.writePPM("render_debug.ppm");
#endif

      render_tick.incAndWait(start);
    }
}

