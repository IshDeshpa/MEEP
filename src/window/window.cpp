#include "window/window.hpp"
#include "config.hpp"
#include "render/camera.hpp"
#include <X11/Xutil.h>
#include <malloc.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <stdexcept>
#ifdef MEEP_XPRA_MOUSE
#include <string_view>
#endif

GameWindow &GameWindow::instance(){
  static GameWindow window;
  return window;
}

GameWindow::GameWindow(){
  // The game thread reads key state while this thread owns the display.
  if(XInitThreads() == 0){
    throw std::runtime_error("Unable to initialize Xlib threads!");
  }

  this->d = XOpenDisplay(NULL);
  if(this->d == NULL){
    throw std::runtime_error("Unable to open display!");
  }

  // construct valid keys real quick
  for(unsigned int k : VALID_KEYSYMS){
    valid_keys.set(XKeysymToKeycode(this->d, k));
  }

  this->s = DefaultScreen(this->d);

  unsigned int black_color = XBlackPixel(this->d, this->s);
  unsigned int white_color = XWhitePixel(this->d, this->s);

  this->w = XCreateSimpleWindow(
    this->d, // display
    RootWindow(this->d, this->s), // parent window
    0, 0, // coordinates of upper left of window
    SCREEN_WIDTH + 5, SCREEN_HEIGHT + 5, // size of window
    2, black_color, // border width, window border color
    black_color // window background color
  );

  XVisualInfo query{};
  query.visualid = XVisualIDFromVisual(DefaultVisual(this->d, this->s));

  int count = 0;
  this->vi = XGetVisualInfo(this->d, VisualIDMask, &query, &count);

  // inform us of window mapping events only
  XSelectInput(this->d, this->w,
               StructureNotifyMask | PointerMotionMask | ButtonPressMask | KeyPressMask |
               KeyReleaseMask | FocusChangeMask);

  Bool repeat_supported = False;
  XkbSetDetectableAutoRepeat(this->d, True, &repeat_supported);
  detectable_repeat = repeat_supported;

  // ask the window manager to send WM_DELETE_WINDOW instead of killing us when the window is closed
  this->wm_delete = XInternAtom(this->d, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(this->d, this->w, &this->wm_delete, 1);

  XClassHint hint = {
    .res_name = const_cast<char *>("powder-game-3d"),
    .res_class = const_cast<char *>("PowderGame3D")
  };
  XSetClassHint(this->d, this->w, &hint);
  XStoreName(this->d, this->w, "Powder Game 3D");

  // Ask the server to map the window to the display
  XMapWindow(this->d, this->w);

  // "Graphics context" ig????
  // State for drawing context (drawing color, thickness, etc.)
  // We need to pass to the GPU whenever we draw stuff
  this->gc = XCreateGC(this->d, this->w, 0, NULL);
  XSetForeground(this->d, this->gc, white_color);

  XFlush(this->d);

  // XImage
  this->img = XCreateImage(
    this->d,
    DefaultVisual(this->d, this->s),
    DefaultDepth(this->d, this->s),
    ZPixmap, // pixmap format
    0, // number of pixels to offset by
    NULL, // image data (nothing rn)
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    32, // padding between scanlines
    0
  );

  // allocate the image buffer
  this->img->data = new char[img->bytes_per_line * img->height];

  // start unfocused so as to not startle the user
  this->focused = false;

  // xinput stuff
  int event_base, error_base;
  if (!XQueryExtension(this->d, "XInputExtension",
                      &this->xi_opcode, &event_base, &error_base)) {
    throw std::runtime_error("XInput extension is unavailable");
  }

  int major = 2;
  int minor = 0;
  if (XIQueryVersion(this->d, &major, &minor) != Success) {
    throw std::runtime_error("XInput2 is unavailable");
  }

  // Raw events are selected on the root window.
  unsigned char mask_bytes[XIMaskLen(XI_RawMotion)] = {};
  XISetMask(mask_bytes, XI_RawMotion);

  XIEventMask mask{};
  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = sizeof(mask_bytes);
  mask.mask = mask_bytes;

  XISelectEvents(this->d, DefaultRootWindow(d), &mask, 1);
#ifdef MEEP_WSL_MOUSE
  unsigned char device_mask_bytes[XIMaskLen(XI_HierarchyChanged)] = {};
  XISetMask(device_mask_bytes, XI_DeviceChanged);
  XISetMask(device_mask_bytes, XI_HierarchyChanged);
  XIEventMask device_mask{XIAllDevices, sizeof(device_mask_bytes), device_mask_bytes};
  XISelectEvents(d, DefaultRootWindow(d), &device_mask, 1);
#endif
  XFlush(this->d);

  // block until the window is mapped
  XEvent e;
  do {
    XNextEvent(this->d, &e);
  } while(e.type != MapNotify);
}

GameWindow::~GameWindow(){
  releaseInput();
  delete []this->img->data;
  XFreeGC(this->d, this->gc);
  XDestroyWindow(this->d, this->w);
  XCloseDisplay(this->d);
}

bool GameWindow::grabInput(){
  if(XGrabPointer(d, w, True, PointerMotionMask | ButtonPressMask,
                  GrabModeAsync, GrabModeAsync, w, None, CurrentTime) != GrabSuccess){
    return false;
  }

  if(XGrabKeyboard(d, w, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess){
    XUngrabPointer(d, CurrentTime);
    XFlush(d);
    return false;
  }

#ifdef MEEP_WSL_MOUSE
  wsl_pointer_sources.clear();
#endif
  return true;
}

void GameWindow::releaseInput(){
#ifdef MEEP_WSL_MOUSE
  wsl_pointer_sources.clear();
  {
    std::lock_guard lock(mouse_move_mutex);
    pending_mouse_move = {0, 0};
  }
#endif
  {
    std::lock_guard lock(pressed_keys_mutex);
    held_keys.reset();
  }
  XUngrabPointer(d, CurrentTime);
  XUngrabKeyboard(d, CurrentTime);
  XFlush(d);
}

void GameWindow::updateScreen(HostScreen &s){
  for(std::size_t i = 0; i < s.WIDTH; i++){
    for(std::size_t j = 0; j < s.HEIGHT; j++){
      XPutPixel(this->img, i, j, s.framebuffer[j*s.WIDTH + i].getNativePixel(this->vi));
    }
  }

  XPutImage(
    this->d, 
    this->w, 
    this->gc, 
    this->img, 
    0, 0,
    0, 0,
    this->img->width,
    this->img->height
  );

  XFlush(this->d);
}

std::bitset<256> GameWindow::pressedKeys(){
  std::lock_guard lock(pressed_keys_mutex);
  return held_keys;
}

#ifdef MEEP_WSL_MOUSE
std::pair<double, double> GameWindow::consumeMouseMove(){
#else
std::pair<int, int> GameWindow::consumeMouseMove(){
#endif
  std::lock_guard lock(mouse_move_mutex);
  return std::exchange(pending_mouse_move, {});
}

std::pair<bool, bool> GameWindow::consumeMouseClicks(){
  std::lock_guard lock(mouse_click_mutex);
  return {std::exchange(pending_left_click, false),
          std::exchange(pending_right_click, false)};
}

#ifdef MEEP_WSL_MOUSE
void GameWindow::handleWslMouseMotion(const XIRawEvent &motion) {
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
          const int extent = valuator->number == 0 ? DisplayWidth(d, s) : DisplayHeight(d, s);
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

void GameWindow::join(){
  XEvent e;
  while(true){
    XNextEvent(this->d, &e); // blocking call

    if(e.type == DestroyNotify){
      return;
    }

    if(e.type == ClientMessage && static_cast<Atom>(e.xclient.data.l[0]) == this->wm_delete){
      return;
    }

    if (e.type == FocusOut && e.xfocus.mode != NotifyGrab && e.xfocus.mode != NotifyUngrab) {
      focused = false;
      releaseInput();
    }

    if (e.type == GenericEvent &&
        e.xcookie.extension == xi_opcode &&
        XGetEventData(d, &e.xcookie)) {

#ifdef MEEP_WSL_MOUSE
      if (e.xcookie.evtype == XI_DeviceChanged ||
          e.xcookie.evtype == XI_HierarchyChanged) {
        wsl_pointer_sources.clear();
      }
#endif

      if (e.xcookie.evtype == XI_RawMotion && focused) {
        const auto* motion =
            static_cast<const XIRawEvent*>(e.xcookie.data);

#ifdef MEEP_WSL_MOUSE
        handleWslMouseMotion(*motion);
#else
#ifdef MEEP_XPRA_MOUSE
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
#ifdef MEEP_XPRA_MOUSE
        bool has_x = false, has_y = false;
#endif

        // raw_values is packed: it contains one value for each set bit.
        for (int axis = 0; axis < motion->valuators.mask_len * 8; ++axis) {
          if (!XIMaskIsSet(motion->valuators.mask, axis)) {
            continue;
          }

          if (axis == 0) {
            dx = *value;
#ifdef MEEP_XPRA_MOUSE
            has_x = true;
#endif
          }
          if (axis == 1) {
            dy = *value;
#ifdef MEEP_XPRA_MOUSE
            has_y = true;
#endif
          }
          ++value;
        }

#ifdef MEEP_XPRA_MOUSE
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

    if (e.type == ButtonPress) {
      if (e.xbutton.button == Button1) {
        if(!this->focused){
          // if we're not focused and we detect a click event in the window, refocus and intercept the click event
          this->focused = grabInput();
        }

        std::lock_guard lock(mouse_click_mutex);
        pending_left_click = true;
      } else if (e.xbutton.button == Button3) {
        std::lock_guard lock(mouse_click_mutex);
        pending_right_click = true;
      }
    }

    if (e.type == KeyRelease) {
      if (!detectable_repeat && XEventsQueued(d, QueuedAfterReading) > 0) {
        XEvent next;
        XPeekEvent(d, &next);
        if (next.type == KeyPress && next.xkey.keycode == e.xkey.keycode &&
            next.xkey.time == e.xkey.time) continue;
      }
      std::lock_guard lock(pressed_keys_mutex);
      held_keys.reset(e.xkey.keycode);
    }

    if (e.type == KeyPress ) {
      if (valid_keys.test(e.xkey.keycode)) {
        std::lock_guard lock(pressed_keys_mutex);
        held_keys.set(e.xkey.keycode);
      }

      if(XLookupKeysym(&e.xkey, 0) == XK_Escape){
        this->focused = false;
#ifdef MEEP_XPRA_MOUSE
        for (auto &[id, source] : pointer_sources) {
          (void)id;
          source.have_position = false;
        }
#endif
        releaseInput();
      }
    }
  }
}
