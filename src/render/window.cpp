#include "render/window.hpp"
#include "config.hpp"
#include "render/camera.hpp"
#include <X11/Xutil.h>
#include <malloc.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <stdexcept>
#include <bit>

static unsigned getNativePixel(Color color, const XVisualInfo *vi){
  const auto red = static_cast<unsigned>(vi->red_mask);
  const auto green = static_cast<unsigned>(vi->green_mask);
  const auto blue = static_cast<unsigned>(vi->blue_mask);
  return ((color.r << std::countr_zero(red)) & red) |
         ((color.g << std::countr_zero(green)) & green) |
         ((color.b << std::countr_zero(blue)) & blue);
}

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

  XSelectInput(this->d, this->w,
               StructureNotifyMask | PointerMotionMask | ButtonPressMask | KeyPressMask | KeyReleaseMask | FocusChangeMask);

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

  mouse.initialize(d, s);

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

  mouse.onGrab();
  return true;
}

void GameWindow::releaseInput(){
  mouse.onRelease();
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
      XPutPixel(this->img, i, j, getNativePixel(s.framebuffer[j*s.WIDTH + i], this->vi));
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

MouseInput::Movement GameWindow::consumeMouseMove(){
  return mouse.consumeMouseMove();
}

std::pair<bool, bool> GameWindow::consumeMouseClicks(){
  return mouse.consumeMouseClicks();
}

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

    mouse.handleEvent(e, focused);

    if (e.type == ButtonPress) {
      if (e.xbutton.button == Button1) {
        if(!this->focused){
          // if we're not focused and we detect a click event in the window, refocus and intercept the click event
          this->focused = grabInput();
        }

        mouse.recordClick(Button1);
      } else if (e.xbutton.button == Button3) {
        mouse.recordClick(Button3);
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
        mouse.resetAbsolutePosition();
        releaseInput();
      }
    }
  }
}
