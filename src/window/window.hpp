#pragma once

#include "render/camera.hpp"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <bitset>
#include <mutex>
#include <unordered_set>
#include <utility>

class GameWindow {
public:
  static GameWindow &instance();

  // Return valid key presses received since the previous call.
  std::bitset<256> consumePressedKeys();

  // Block until the user closes the window
  void join();

  static bool isPressed(const char c, const std::bitset<256>& state){
    KeySym s = XStringToKeysym(std::string(1, c).c_str());

    KeySym lower = NoSymbol;
    KeySym upper = NoSymbol;
    XConvertCase(s, &lower, &upper);

    if(lower == NoSymbol) return false;

    return state.test(XKeysymToKeycode(instance().d, lower));
  }

  void updateScreen(HostScreen &s);

  // Returns the accumulated movement since the previous call, or {0, 0} if none.
  std::pair<int, int> consumeMouseMove();

  // Returns {left_clicked, right_clicked} since the previous call.
  std::pair<bool, bool> consumeMouseClicks();

private:
  GameWindow();
  ~GameWindow();

  Display *d;
  XVisualInfo *vi;
  int s;
  Window w;
  GC gc;
  XImage *img;

  Atom wm_delete;

  bool focused;
  std::mutex mouse_move_mutex;
  std::pair<int, int> pending_mouse_move{0, 0};
  std::mutex pressed_keys_mutex;
  std::bitset<256> pending_keys;
  std::mutex mouse_click_mutex;
  bool pending_left_click{false};
  bool pending_right_click{false};

  // Set of keys that are considered valid (keycodes, not keysyms)
  const std::unordered_set<unsigned int> VALID_KEYSYMS = {XK_W, XK_A, XK_S, XK_D};
  std::bitset<256> valid_keys;
};

