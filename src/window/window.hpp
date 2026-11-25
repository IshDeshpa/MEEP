#pragma once

#include "config.hpp"

#include "render/camera.hpp"
#if MEEP_MOUSE_MODE == 1
#include "window/pointer_motion.hpp"
#endif
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <bitset>
#include <mutex>
#if MEEP_MOUSE_MODE != 0
#include <unordered_map>
#endif
#include <unordered_set>
#include <utility>

class GameWindow {
public:
  static GameWindow &instance();

  // Return a snapshot of currently held valid keys.
  std::bitset<256> pressedKeys();

  // Block until the user closes the window
  void join();

  static bool isPressed(const char c, const std::bitset<256>& state, bool shift = false){
    KeySym s = c == ' ' ? XK_space : XStringToKeysym(std::string(1, c).c_str());

    KeySym lower = NoSymbol;
    KeySym upper = NoSymbol;
    XConvertCase(s, &lower, &upper);

    if(lower == NoSymbol) return false;

    const auto &window = instance();
    const KeyCode key = XKeysymToKeycode(window.d, lower);
    const KeyCode left = XKeysymToKeycode(window.d, XK_Shift_L);
    const KeyCode right = XKeysymToKeycode(window.d, XK_Shift_R);
    return key != 0 && state.test(key) &&
           (!shift || (left != 0 && state.test(left)) ||
                      (right != 0 && state.test(right)));
  }

  void updateScreen(HostScreen &s);

  // Returns the accumulated movement since the previous call, or {0, 0} if none.
#if MEEP_MOUSE_MODE == 1
  std::pair<double, double> consumeMouseMove();
#else
  std::pair<int, int> consumeMouseMove();
#endif

  // Returns {left_clicked, right_clicked} since the previous call.
  std::pair<bool, bool> consumeMouseClicks();

private:
  GameWindow();
  ~GameWindow();

  bool grabInput();
  void releaseInput();
#if MEEP_MOUSE_MODE == 1
  void handleWslMouseMotion(const XIRawEvent &motion);
#endif

  Display *d;
  XVisualInfo *vi;
  int s;
  Window w;
  GC gc;
  XImage *img;

  Atom wm_delete;

  int xi_opcode;
  bool detectable_repeat = false;
#if MEEP_MOUSE_MODE == 1
  std::unordered_map<int, PointerMotion> wsl_pointer_sources;
#endif
#if MEEP_MOUSE_MODE == 2
  struct PointerSource {
    bool absolute_synthetic = false;
    bool have_position = false;
    double x = 0, y = 0;
  };
  std::unordered_map<int, PointerSource> pointer_sources;
#endif

  bool focused;
  std::mutex mouse_move_mutex;
#if MEEP_MOUSE_MODE == 1
  std::pair<double, double> pending_mouse_move{0, 0};
#else
  std::pair<int, int> pending_mouse_move{0, 0};
#endif
  std::mutex pressed_keys_mutex;
  std::bitset<256> held_keys;
  std::mutex mouse_click_mutex;
  bool pending_left_click{false};
  bool pending_right_click{false};

  // Set of keys that are considered valid (keycodes, not keysyms)
  const std::unordered_set<unsigned int> VALID_KEYSYMS = {XK_W, XK_A, XK_S, XK_D, XK_space, XK_Shift_L, XK_Shift_R};
  std::bitset<256> valid_keys;
};

