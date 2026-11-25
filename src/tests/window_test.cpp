#include "window/window.hpp"
#include <fstream>
#include <iostream>
#include <stop_token>
#include <stdexcept>
#include <thread>
#include <unistd.h>

static constexpr unsigned TICK_SECS = 1;
static constexpr float RENDER_TICK_SECS = 41.6f;

void dummyTick(std::stop_token stop){
  GameWindow &window = GameWindow::instance();

  while(!stop.stop_requested()){
    // Read the currently held keys once per tick.
    std::bitset<256> state = window.pressedKeys();

    if(window.isPressed('w', state)){
      std::cout << "W";
    }

    if(window.isPressed('a', state)){
      std::cout << "A";
    }

    if(window.isPressed('s', state)){
      std::cout << "S";
    }

    if(window.isPressed('d', state)){
      std::cout << "D";
    }

    const auto [dx, dy] = window.consumeMouseMove();
    if(dx != 0 || dy != 0){
      std::cout << " mouse: dx=" << dx << " dy=" << dy;
    }

    std::cout << std::endl;

    sleep(TICK_SECS);
  }
}

void dummyRender(std::stop_token stop){
  GameWindow &window = GameWindow::instance();

  std::ifstream image("cat.ppm", std::ios::binary);
  std::string format;
  std::size_t width;
  std::size_t height;
  unsigned max_value;
  image >> format >> width >> height >> max_value;
  image.get();

  if(!image || format != "P6" || max_value != 255){
    throw std::runtime_error("Unable to read cat.ppm");
  }

  HostScreen screen(width, height);
  for(Color &pixel : screen.framebuffer){
    image.read(reinterpret_cast<char *>(&pixel.r), 1);
    image.read(reinterpret_cast<char *>(&pixel.g), 1);
    image.read(reinterpret_cast<char *>(&pixel.b), 1);
    pixel.a = 255;
  }

  if(!image){
    throw std::runtime_error("Unable to read cat.ppm pixel data");
  }

  while(!stop.stop_requested()){
    window.updateScreen(screen);

    sleep(RENDER_TICK_SECS);
  }
}

int main(){
  GameWindow &window = GameWindow::instance();

  std::jthread t1 = std::jthread(&dummyTick);
  std::jthread t2 = std::jthread(&dummyRender);

  // a window should appear! run until the user closes it
  window.join();

  // stop the tick thread; the jthread joins when it goes out of scope
  t1.request_stop();
  t2.request_stop();
}
