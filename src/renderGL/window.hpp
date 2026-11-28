#pragma once

#include <GL/glew.h>
#include <GL/glx.h>
#include "mouse/mouse.hpp"
#include <X11/Xutil.h>
#include <bitset>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <utility>


class GameWindow {
public:
  static GameWindow &instance();

  GameWindow(const GameWindow &) = delete;
  GameWindow &operator=(const GameWindow &) = delete;

  // to be used by render thread
  void initGL();
  void shutdownGL();
  // Call only while context initialization and shutdown are not running.
  bool isGLContextCurrent() const;

  std::bitset<256> pressedKeys();
  static bool isPressed(char c, const std::bitset<256>& state, bool shift = false);
  void join();
  MouseInput::Movement consumeMouseMove();
  std::pair<bool, bool> consumeMouseClicks();

private:
  GameWindow();
  ~GameWindow();
  bool grabInput();
  void releaseInput();

  Display *disp = nullptr;
  int screen = 0;
  Window window = 0;
  Colormap cmap = 0;
  GLXFBConfig config = nullptr;
  GLXContext gl_context = nullptr;
  std::thread::id gl_thread;
  Atom wm_delete = None;
  MouseInput mouse;
  bool focused = false;
  bool detectable_repeat = false;

  std::mutex pressed_keys_mutex;
  std::bitset<256> held_keys;
  const std::unordered_set<unsigned int> VALID_KEYSYMS = {
      XK_W, XK_A, XK_S, XK_D, XK_space, XK_Shift_L, XK_Shift_R};
  std::bitset<256> valid_keys;

};
