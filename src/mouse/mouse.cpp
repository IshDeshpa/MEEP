#include "mouse/mouse.hpp"
#include <stdexcept>
#include <string_view>
#include <utility>

void MouseInput::initialize(Display *display, int screen_number){
  d = display;
  screen = screen_number;
  // xinput stuff
  int event_base, error_base;
  if (!XQueryExtension(d, "XInputExtension",
                      &xi_opcode, &event_base, &error_base)) {
    throw std::runtime_error("XInput extension is unavailable");
  }

  int major = 2;
  int minor = 0;
  if (XIQueryVersion(d, &major, &minor) != Success) {
    throw std::runtime_error("XInput2 is unavailable");
  }

  // Raw events are selected on the root window.
  unsigned char mask_bytes[XIMaskLen(XI_RawMotion)] = {};
  XISetMask(mask_bytes, XI_RawMotion);

  XIEventMask mask{};
  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = sizeof(mask_bytes);
  mask.mask = mask_bytes;

  XISelectEvents(d, DefaultRootWindow(d), &mask, 1);
#if MEEP_MOUSE_MODE == 1
  unsigned char device_mask_bytes[XIMaskLen(XI_HierarchyChanged)] = {};
  XISetMask(device_mask_bytes, XI_DeviceChanged);
  XISetMask(device_mask_bytes, XI_HierarchyChanged);
  XIEventMask device_mask{XIAllDevices, sizeof(device_mask_bytes), device_mask_bytes};
  XISelectEvents(d, DefaultRootWindow(d), &device_mask, 1);
#endif
  XFlush(d);

}

void MouseInput::onGrab(){
#if MEEP_MOUSE_MODE == 1
  wsl_pointer_sources.clear();
#endif
}

void MouseInput::onRelease(){
#if MEEP_MOUSE_MODE == 1
  wsl_pointer_sources.clear();
  std::lock_guard lock(mouse_move_mutex);
  pending_mouse_move = {0, 0};
#endif
}

void MouseInput::resetAbsolutePosition(){
#if MEEP_MOUSE_MODE == 2
  for (auto &[id, source] : pointer_sources) {
    (void)id;
    source.have_position = false;
  }
#endif
}

void MouseInput::recordClick(unsigned int button){
  std::lock_guard lock(mouse_click_mutex);
  if (button == Button1) pending_left_click = true;
  if (button == Button3) pending_right_click = true;
}

#if MEEP_MOUSE_MODE == 1
std::pair<double, double> MouseInput::consumeMouseMove(){
#else
std::pair<int, int> MouseInput::consumeMouseMove(){
#endif
  std::lock_guard lock(mouse_move_mutex);
  return std::exchange(pending_mouse_move, {});
}

std::pair<bool, bool> MouseInput::consumeMouseClicks(){
  std::lock_guard lock(mouse_click_mutex);
  return {std::exchange(pending_left_click, false),
          std::exchange(pending_right_click, false)};
}

#if MEEP_MOUSE_MODE == 1
void MouseInput::handleWslMouseMotion(const XIRawEvent &motion) {
  auto [source_it, inserted] = wsl_pointer_sources.try_emplace(motion.sourceid);
  auto &source = source_it->second;
  if (inserted) {
    int count = 0;
    XIDeviceInfo *device = XIQueryDevice(d, motion.sourceid, &count);
    if (device && count > 0) {
      for (int i = 0; i < device[0].num_classes; ++i) {
        if (device[0].classes[i]->type != XIValuatorClass) continue;
        const auto *valuator =
            reinterpret_cast<const XIValuatorClassInfo *>(device[0].classes[i]);
        if (valuator->number < 0 || valuator->number > 1) continue;
        auto &axis = source.axes[valuator->number];
        axis.absolute = valuator->mode == XIModeAbsolute;
        if (axis.absolute && valuator->max > valuator->min) {
          const int extent = valuator->number == 0 ? DisplayWidth(d, screen) : DisplayHeight(d, screen);
          axis.scale = (extent - 1.0) / (valuator->max - valuator->min);
        }
      }
    }
    if (device) XIFreeDeviceInfo(device);
  }
  const auto [dx, dy] = source.readMotion(motion.valuators, motion.raw_values);
  std::lock_guard lock(mouse_move_mutex);
  pending_mouse_move.first += dx;
  pending_mouse_move.second += dy;
}
#endif

void MouseInput::handleEvent(XEvent &e, bool focused){
  if (e.type == GenericEvent &&
        e.xcookie.extension == xi_opcode &&
        XGetEventData(d, &e.xcookie)) {

#if MEEP_MOUSE_MODE == 1
      if (e.xcookie.evtype == XI_DeviceChanged ||
          e.xcookie.evtype == XI_HierarchyChanged) {
        wsl_pointer_sources.clear();
      }
#endif

      if (e.xcookie.evtype == XI_RawMotion && focused) {
        const auto* motion =
            static_cast<const XIRawEvent*>(e.xcookie.data);

#if MEEP_MOUSE_MODE == 1
        handleWslMouseMotion(*motion);
#else
#if MEEP_MOUSE_MODE == 2
        auto [source_it, inserted] = pointer_sources.try_emplace(motion->sourceid);
        auto &source = source_it->second;
        if (inserted) {
          int count = 0;
          XIDeviceInfo *device = XIQueryDevice(d, motion->sourceid, &count);
          if (device && count > 0 && device[0].name) {
            source.absolute_synthetic =
                std::string_view(device[0].name).find("XTEST pointer") != std::string_view::npos;
          }
          if (device) XIFreeDeviceInfo(device);
        }
#endif

        double dx = 0.0;
        double dy = 0.0;
        const double* value = motion->raw_values;
#if MEEP_MOUSE_MODE == 2
        bool has_x = false, has_y = false;
#endif

        // raw_values is packed: it contains one value for each set bit.
        for (int axis = 0; axis < motion->valuators.mask_len * 8; ++axis) {
          if (!XIMaskIsSet(motion->valuators.mask, axis)) {
            continue;
          }

          if (axis == 0) {
            dx = *value;
#if MEEP_MOUSE_MODE == 2
            has_x = true;
#endif
          }
          if (axis == 1) {
            dy = *value;
#if MEEP_MOUSE_MODE == 2
            has_y = true;
#endif
          }
          ++value;
        }

#if MEEP_MOUSE_MODE == 2
        // Xpra injects absolute coordinates through the XTEST pointer even
        // though XInput describes its valuators as relative.
        if (source.absolute_synthetic) {
          const double x = dx, y = dy;
          dx = has_x && source.have_position ? x - source.x : 0;
          dy = has_y && source.have_position ? y - source.y : 0;
          if (has_x) source.x = x;
          if (has_y) source.y = y;
          if (has_x && has_y) source.have_position = true;
        }
#endif

        std::lock_guard lock(mouse_move_mutex);
        pending_mouse_move.first += dx;
        pending_mouse_move.second += dy;
#endif
      }

      XFreeEventData(d, &e.xcookie);
    }

}
