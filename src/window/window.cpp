#include "window/window.hpp"
#include "config.hpp"
#include "render/camera.hpp"
#include <X11/Xutil.h>
#include <malloc.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <stdexcept>

GameWindow &GameWindow::instance(){
  static GameWindow window;
  return window;
}

GameWindow::GameWindow(){
  // pressedKeys() is called from the game thread while this thread owns the display
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
  XSelectInput(this->d, this->w, StructureNotifyMask);

  // ask the window manager to send WM_DELETE_WINDOW instead of killing us when the window is closed
  this->wm_delete = XInternAtom(this->d, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(this->d, this->w, &this->wm_delete, 1);

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

  // block until the window is mapped
  XEvent e;
  do {
    XNextEvent(this->d, &e);
  } while(e.type != MapNotify);
}

GameWindow::~GameWindow(){
  delete []this->img->data;
  XFreeGC(this->d, this->gc);
  XDestroyWindow(this->d, this->w);
  XCloseDisplay(this->d);
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
  char keymap[32];
  XQueryKeymap(this->d, keymap);

  std::bitset<256> held;
  for(int kc = 0; kc < 256; ++kc){
    if(keymap[kc / 8] & (1 << (kc % 8))){
      held.set(kc);
    }
  }

  return held & valid_keys;
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
  }
}
