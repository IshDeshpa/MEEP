#pragma once

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <bitset>
#include <unordered_set>

class GameWindow {
public:
  static GameWindow &instance();

  // Return the valid keycodes that are held down right now
  std::bitset<256> pressedKeys();

  // Block until the user closes the window
  void join();

  static constexpr bool isPressed(const char c, std::bitset<256> state){
    KeySym s = XStringToKeysym(std::string(1, c).c_str());

    KeySym lower = NoSymbol;
    KeySym upper = NoSymbol;
    XConvertCase(s, &lower, &upper);

    if(lower == NoSymbol) return false;

    return state.test(XKeysymToKeycode(instance().d, lower));
  }

private:
  GameWindow();
  ~GameWindow();

  Display *d;
  int s;
  Window w;
  GC gc;
  Atom wm_delete;

  // Set of keys that are considered valid (keycodes, not keysyms)
  const std::unordered_set<unsigned int> VALID_KEYSYMS = {XK_W, XK_A, XK_S, XK_D};
  std::bitset<256> valid_keys;
};

