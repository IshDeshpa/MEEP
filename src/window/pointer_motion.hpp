#pragma once

#include <X11/extensions/XInput2.h>
#include <array>
#include <utility>

// A raw valuator can contain an absolute position rather than a movement.
// Keep independent baselines: XInput may include only one axis in an event.
struct PointerAxis {
  bool absolute = false;
  double scale = 1.0;
  bool have_position = false;
  double position = 0.0;

  double movement(double value) {
    if (!absolute) return value;
    const double delta = have_position ? (value - position) * scale : 0.0;
    position = value;
    have_position = true;
    return delta;
  }
};

struct PointerMotion {
  std::array<PointerAxis, 2> axes;

  std::pair<double, double> readMotion(const XIValuatorState &valuators,
                                      const double *raw_values) {
    std::array<double, 2> delta{};
    // Values are packed, including any wheel/other axes in the mask.
    for (int axis = 0; axis < valuators.mask_len * 8; ++axis) {
      if (!XIMaskIsSet(valuators.mask, axis)) continue;
      if (axis < 2) delta[axis] = axes[axis].movement(*raw_values);
      ++raw_values;
    }
    return {delta[0], delta[1]};
  }
};
