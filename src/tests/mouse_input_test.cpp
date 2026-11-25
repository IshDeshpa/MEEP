#include "config.hpp"
#ifdef MEEP_WSL_MOUSE
#include "window/pointer_motion.hpp"
#endif

#include <cmath>
#include <iostream>
#include <stdexcept>

#ifdef MEEP_WSL_MOUSE
static void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

static bool near(double actual, double expected) {
  return std::abs(actual - expected) < 1e-9;
}

static std::pair<double, double> readMotion(PointerMotion &source,
                                           unsigned char mask,
                                           const double *values) {
  XIValuatorState valuators{1, &mask, nullptr};
  return source.readMotion(valuators, values);
}

#endif

int main() {
#ifdef MEEP_WSL_MOUSE
  PointerMotion relative;
  const double relative_values[]{-4.5, 7.25, 120.0};
  const auto [relative_x, relative_y] = readMotion(relative, 0b111, relative_values);
  check(near(relative_x, -4.5) && near(relative_y, 7.25),
        "Relative deltas must pass through; wheel input must be ignored");

  // Match WSL's absolute 0..65535 axes, normalized into screen pixels.
  PointerMotion absolute;
  absolute.axes[0].absolute = absolute.axes[1].absolute = true;
  absolute.axes[0].scale = 1919.0 / 65535.0;
  absolute.axes[1].scale = 1079.0 / 65535.0;
  const double initial[]{30000.0, 40000.0};
  const auto [initial_x, initial_y] = readMotion(absolute, 0b11, initial);
  check(initial_x == 0.0 && initial_y == 0.0,
        "The first absolute position must not turn the camera");
  const double moved[]{30000.0 + 65535.0 / 1919.0,
                       40000.0 - 65535.0 / 1079.0};
  const auto [moved_x, moved_y] = readMotion(absolute, 0b11, moved);
  check(near(moved_x, 1.0) && near(moved_y, -1.0),
        "Absolute motion must become signed pixel deltas");
  const auto [back_x, back_y] = readMotion(absolute, 0b11, initial);
  check(near(back_x, -1.0) && near(back_y, 1.0),
        "Reversing absolute movement must reverse rotation");

  // WSL sources may send only one coordinate. Seed each baseline independently.
  PointerMotion sparse_source;
  sparse_source.axes[0].absolute = sparse_source.axes[1].absolute = true;
  const double first_x[]{500.0}, first_y[]{350.0}, second_x[]{490.0};
  check(readMotion(sparse_source, 0b01, first_x).first == 0.0 &&
        readMotion(sparse_source, 0b10, first_y).second == 0.0,
        "Sparse first events must establish independent baselines");
  const auto [sparse_x, sparse_y] = readMotion(sparse_source, 0b01, second_x);
  check(sparse_x == -10.0 && sparse_y == 0.0,
        "An absent axis must retain its baseline and report zero movement");
  const double wheel[]{120.0};
  check(readMotion(sparse_source, 0b100, wheel) == std::pair{0.0, 0.0},
        "Wheel-only events must not move the camera");
  check(readMotion(sparse_source, 0b11, initial) != std::pair{0.0, 0.0},
        "A previously seen source must retain its own baseline");
  absolute = PointerMotion{};
  absolute.axes[0].absolute = absolute.axes[1].absolute = true;
  check(readMotion(absolute, 0b11, initial) == std::pair{0.0, 0.0},
        "After recapture, the old absolute baseline must not cause a jump");

#endif

#if defined(MEEP_WSL_MOUSE) || defined(MEEP_XPRA_MOUSE)
  static_assert(MOUSE_SENSITIVITY == 0.002);
#else
  static_assert(MOUSE_SENSITIVITY == 0.00001);
#endif
#ifdef MEEP_WSL_MOUSE
  std::cout << "WSL mouse input conversion and sensitivity checks passed\n";
#else
  std::cout << "Legacy mouse sensitivity defaults checked\n";
#endif
}
