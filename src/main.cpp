#include "render/render.hpp"
#include "state/state.hpp"
#include "state/lut.hpp"
#include "tick/tick.hpp"
#include "window/window.hpp"

#include <charconv>
#include <condition_variable>
#include <exception>
#include <glm/geometric.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <string_view>
#include <chrono>
#include <thread>

TickSource world_tick{WORLD_TICK_RATE};
TickSource render_tick{RENDER_TICK_RATE};
TickSource input_tick{INPUT_TICK_RATE};

std::unique_ptr<WorldState> world;
std::unique_ptr<LUT> lut;

std::mutex render_sync_mutex;
std::condition_variable_any render_sync;
bool frame_ready = false;
bool frame_rendered = true;

using Clock = std::chrono::steady_clock;

void inputUpdate(GameWindow& window, Camera& camera){
  std::bitset<256> keys = window.consumePressedKeys(); 
  std::pair<int, int> dx_dy = window.consumeMouseMove(); 
  int &dx = dx_dy.first; 
  int &dy = dx_dy.second;
  std::pair<bool, bool> left_right = window.consumeMouseClicks(); 
  [[maybe_unused]] bool &left = left_right.first; 
  [[maybe_unused]] bool &right = left_right.second;

  // move look
  camera.rotateLookDir(dx, dy);
  
  // move pos
  if(window.isPressed('w', keys)){
    camera.moveForward(PLAYER_SPEED);
  }

  if(window.isPressed('a', keys)){
    camera.moveRight(PLAYER_SPEED);
  }

  if(window.isPressed('w', keys)){
    camera.moveForward(-PLAYER_SPEED);
  }

  if(window.isPressed('d', keys)){
    camera.moveRight(-PLAYER_SPEED);
  }
}

void inputTick(std::stop_token stop_token, GameWindow& window, Camera& camera) {
  while (!stop_token.stop_requested()) {
    const time_point start = Clock::now();
    inputUpdate(window, camera);
    input_tick.incAndWait(start);
  }
}

void worldTick(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    time_point start = Clock::now();

    world->updateWorld(world_tick.counter, *lut);
    world->waitForTick();
    world->swapBuffers();

    world_tick.incAndWait(start);
    {
      std::lock_guard lock(render_sync_mutex);
      frame_ready = true;
      frame_rendered = false;
    }
    render_sync.notify_one();

    std::unique_lock lock(render_sync_mutex);
    if (!render_sync.wait(lock, stop_token, [] { return frame_rendered; })) {
      return;
    }
  }
}

void renderTick(std::stop_token stop_token, GameWindow& window,
                Renderer& renderer, Camera& camera) {
    const glm::dvec3 pos(-5.0, 3.0, 3.0);
    const glm::dvec3 target(1.0, 0.0, 0.0);
    camera.setPose(pos, glm::normalize(target - pos));

    while (!stop_token.stop_requested()) {
      std::unique_lock lock(render_sync_mutex);
      if (!render_sync.wait(lock, stop_token, [] { return frame_ready; })) {
        return;
      }
      lock.unlock();

      time_point start = Clock::now();

      stateToWorldLaunch(*world);
      worldToClipLaunch();
      clipVertsLaunch();
      clipToNDCLaunch();
      ndcToScreenLaunch();
      rasterizeLaunch();

      // update window
      HostScreen s = renderer.device_screen->hostScreen();
      window.updateScreen(s);

      render_tick.incAndWait(start);

      {
        std::lock_guard done_lock(render_sync_mutex);
        frame_ready = false;
        frame_rendered = true;
      }
      render_sync.notify_one();
    }
}

int main(int argc, char* argv[]){
  size_t n = 100, m = 100, k = 100;

  for (int i = 1; i < argc; ++i) {
    std::string_view option = argv[i];
    if (option == "--help" || option == "-h") {
      std::cout << "Usage: " << argv[0]
                << " [-n N] [-m M] [-k K]\n"
                << "Aliases: -x for -n, -y for -m, -z for -k.\n"
                << "Visible dimensions must be positive integers; each defaults to 100.\n"
                << "Storage adds one BLOCK cell on each side (+2 per dimension).\n";
      return 0;
    }

    size_t* dimension = nullptr;
    if (option == "-n" || option == "-x") {
      dimension = &n;
    } else if (option == "-m" || option == "-y") {
      dimension = &m;
    } else if (option == "-k" || option == "-z") {
      dimension = &k;
    } else {
      std::cerr << "Unknown option: " << option << '\n';
      return 1;
    }

    if (++i == argc) {
      std::cerr << "Missing value for " << option << '\n';
      return 1;
    }
    const std::string_view value = argv[i];
    size_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0) {
      std::cerr << "Invalid dimension for " << option << ": " << value
                << " (expected a positive integer)\n";
      return 1;
    }
    *dimension = parsed;
  }

  try {
    world = std::make_unique<WorldState>(n, m, k);
    lut = std::make_unique<LUT>("lut.bin");

    GameWindow& window = GameWindow::instance();
    Renderer& renderer = Renderer::instance();
    Camera& camera = Camera::instance();

    // start world running function
    //    needs to take tick rate, which can be changed by user during runtime, into account. 
    //    needs to maybe sync with rendering? 
    //    needs to be able to take in input from user modifying world (maybe not because state mem is unifed, maybe some flag to keep sync?)
    // function to contain world ticking loop, which calls the individual tick cuda kernel, which is global,
    //    which calls all the tile update functions for all tiles computed in that tick, which is host/device? 
    //    which cells are in which tile depends on phase, which is tick_counter & b'0010 (phase changes every 2 ticks)
    //    which 2 of 4 faces are computed depends on tick_counter & b'0001 (x faces then z faces then repeat)

    std::jthread t1{worldTick};
    std::jthread t2{[&](std::stop_token stop_token) {
      renderTick(stop_token, window, renderer, camera);
    }};
    std::jthread t3{[&](std::stop_token stop_token) {
      inputTick(stop_token, window, camera);
    }};
    
    window.join();
  
    t1.request_stop();
    t2.request_stop();
    t3.request_stop();

  } catch (const std::exception& error) {
    std::cerr << "Could not create world: " << error.what() << '\n';
    return 1;
  }
}
