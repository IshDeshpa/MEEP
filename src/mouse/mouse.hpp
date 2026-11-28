#pragma once

#include "config.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <mutex>
#include <unordered_map>
#include <utility>
#if MEEP_MOUSE_MODE == 1
#include "mouse/pointer_motion.hpp"
#endif

// The window owns the X display and dispatches events on its event thread.
class MouseInput {
public:
#if MEEP_MOUSE_MODE == 1
  using Movement = std::pair<double, double>;
#else
  using Movement = std::pair<int, int>;
#endif
  void initialize(Display *display, int screen_number);
  void handleEvent(XEvent &event, bool focused);
  void recordClick(unsigned int button);
  void onGrab();
  void onRelease();
  void resetAbsolutePosition();
  Movement consumeMouseMove();
  std::pair<bool, bool> consumeMouseClicks();

private:
  Display *d = nullptr;
  int screen = 0;
  int xi_opcode = 0;
  std::mutex mouse_move_mutex;
  Movement pending_mouse_move{0, 0};
  std::mutex mouse_click_mutex;
  bool pending_left_click = false;
  bool pending_right_click = false;
#if MEEP_MOUSE_MODE == 1
  void handleWslMouseMotion(const XIRawEvent &motion);
  std::unordered_map<int, PointerMotion> wsl_pointer_sources;
#elif MEEP_MOUSE_MODE == 2
  struct PointerSource {
    bool absolute_synthetic = false;
    bool have_position = false;
    double x = 0, y = 0;
  };
  std::unordered_map<int, PointerSource> pointer_sources;
#endif
};
