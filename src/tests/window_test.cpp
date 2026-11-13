#include "window/window.hpp"
#include <iostream>
#include <thread>
#include <unistd.h>

static constexpr unsigned TICK_SECS = 1;

void dummyTick(std::stop_token stop){
  GameWindow &window = GameWindow::instance();

  while(!stop.stop_requested()){
    // get the currently pressed keys once per tick on the tick edge
    // if you're faster than a tick then unfortunately your keypress will not register, so ticks must be 
    // short enough for the edge to trigger
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

    std::cout << std::endl;

    sleep(TICK_SECS);
  }
}

int main(){
  GameWindow &window = GameWindow::instance();

  std::jthread t = std::jthread(&dummyTick);

  // a window should appear! run until the user closes it
  window.join();

  // stop the tick thread; the jthread joins when it goes out of scope
  t.request_stop();
}
