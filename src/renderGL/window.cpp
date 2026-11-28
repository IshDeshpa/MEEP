#include "renderGL/window.hpp"
#include "config.hpp"
#include <GL/glew.h>
#include <GL/glx.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <stdexcept>
#include <string>
#include <string_view>

GameWindow &GameWindow::instance(){
   static GameWindow window;
   return window;
}

GameWindow::GameWindow(){
   if (XInitThreads() == 0) {
      throw std::runtime_error("Unable to initialize Xlib threads!");
   }

   this->disp = XOpenDisplay(nullptr);
   if (!this->disp) {
      throw std::runtime_error("Cannot open X Display!");
   }

   this->screen = DefaultScreen(this->disp);
   for (unsigned int key : VALID_KEYSYMS) {
      const KeyCode code = XKeysymToKeycode(this->disp, key);
      if (code != 0) valid_keys.set(code);
   }
   Bool repeat_supported = False;
   XkbSetDetectableAutoRepeat(this->disp, True, &repeat_supported);
   detectable_repeat = repeat_supported;

   int attrib_list[] = {
    // Configuration must support rendering to an X window.
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,

    // Render with RGBA colors.
    GLX_RENDER_TYPE, GLX_RGBA_BIT,

    // Configuration must have an associated X visual.
    GLX_X_RENDERABLE, True,

    // Use a TrueColor X visual.
    GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,

    // At least 8 bits per color channel.
    GLX_RED_SIZE,   8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE,  8,
    GLX_ALPHA_SIZE, 8,

    // At least 24 bits for the depth buffer.
    GLX_DEPTH_SIZE, 24,

    // Require front and back color buffers.
    GLX_DOUBLEBUFFER, True,

    // End of the attribute list.
    None
   };

   int config_count = 0;
   GLXFBConfig* configs = glXChooseFBConfig(
      this->disp, 
      this->screen, 
      attrib_list,
      &config_count
   );

   if(!configs || config_count == 0){
      if (configs) XFree(configs);
      XCloseDisplay(this->disp);
      throw std::runtime_error("No valid GLX framebuffer config!");
   }

   this->config = configs[0];
   XFree(configs);

   XVisualInfo* visual = glXGetVisualFromFBConfig(this->disp, this->config);

   if(!visual){
      XCloseDisplay(this->disp);
      throw std::runtime_error("No X visual for GLX config!");
   }

   this->cmap = XCreateColormap(
      this->disp, 
      RootWindow(this->disp, visual->screen), 
      visual->visual, 
      AllocNone
   );

   XSetWindowAttributes attrs{};
   attrs.colormap = this->cmap;
   attrs.border_pixel = BlackPixel(this->disp, this->screen);
   attrs.background_pixel = BlackPixel(this->disp, this->screen);
   attrs.event_mask = StructureNotifyMask | PointerMotionMask | ButtonPressMask | KeyPressMask | KeyReleaseMask | FocusChangeMask;

   this->window = XCreateWindow(
      this->disp, RootWindow(this->disp, visual->screen),
      0, 0, SCREEN_WIDTH + 5, SCREEN_HEIGHT + 5, 2,
      visual->depth, InputOutput, visual->visual,
      CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &attrs);

   if (!this->window) {
      XFree(visual);
      XFreeColormap(this->disp, this->cmap);
      XCloseDisplay(this->disp);
      throw std::runtime_error("Unable to create X window!");
   }

   XFree(visual);
   try {
      mouse.initialize(this->disp, this->screen);
   } catch (...) {
      XDestroyWindow(this->disp, this->window);
      XFreeColormap(this->disp, this->cmap);
      XCloseDisplay(this->disp);
      throw;
   }

   this->wm_delete = XInternAtom(this->disp, "WM_DELETE_WINDOW", False);
   XSetWMProtocols(this->disp, this->window, &this->wm_delete, 1);
   XClassHint hint{
      .res_name = const_cast<char *>("powder-game-3d"),
      .res_class = const_cast<char *>("PowderGame3D")
   };
   XSetClassHint(this->disp, this->window, &hint);
   XStoreName(this->disp, this->window, "Powder Game 3D");
   XMapWindow(this->disp, this->window);
   XFlush(this->disp);

   XEvent event;
   do {
      XNextEvent(this->disp, &event);
   } while (event.type != MapNotify || event.xmap.window != this->window);
}

GameWindow::~GameWindow(){
   releaseInput();
   if (gl_context) {
      if (glXGetCurrentContext() == gl_context) glXMakeCurrent(disp, None, nullptr);
      glXDestroyContext(disp, gl_context);
   }
   XDestroyWindow(this->disp, this->window);
   XFreeColormap(this->disp, this->cmap);
   XCloseDisplay(this->disp);
}

void GameWindow::initGL(){
  if (gl_context) throw std::runtime_error("GL context is already initialized");
  const char *extensions = glXQueryExtensionsString(disp, screen);
  auto has_extension = [extensions](std::string_view name) {
    if (!extensions) return false;
    std::string_view list(extensions);
    std::size_t pos = 0;
    while (pos < list.size()) {
      const auto end = list.find(' ', pos);
      if (list.substr(pos, end == std::string_view::npos ? end : end - pos) == name)
        return true;
      if (end == std::string_view::npos) break;
      pos = end + 1;
    }
    return false;
  };
  if (!has_extension("GLX_ARB_create_context") ||
      !has_extension("GLX_ARB_create_context_profile"))
    throw std::runtime_error("GLX does not support core-profile context creation");

  using CreateContextAttribs = GLXContext (*)(
      Display*, GLXFBConfig, GLXContext, Bool, const int*);
  auto create_context = reinterpret_cast<CreateContextAttribs>(
      glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
  if (!create_context) throw std::runtime_error("glXCreateContextAttribsARB is unavailable");

  const int attributes[] = {
      GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
      GLX_CONTEXT_MINOR_VERSION_ARB, 3,
      GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
      None
  };
  GLXContext context = create_context(disp, config, nullptr, True, attributes);
  if (!context) throw std::runtime_error("Unable to create OpenGL 3.3 context");
  if (!glXMakeCurrent(disp, window, context)) {
    glXDestroyContext(disp, context);
    throw std::runtime_error("Unable to make GLX context current");
  }

  glewExperimental = GL_TRUE;
  const GLenum status = glewInit();

  if (status != GLEW_OK) {
    const std::string message(reinterpret_cast<const char*>(glewGetErrorString(status)));
    glXMakeCurrent(disp, None, nullptr);
    glXDestroyContext(disp, context);
    throw std::runtime_error("GLEW initialization failed: " + message);
  }
  gl_context = context;
  gl_thread = std::this_thread::get_id();
  glViewport(0, 0, SCREEN_WIDTH + 5, SCREEN_HEIGHT + 5);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glFrontFace(GL_CW);
}

bool GameWindow::isGLContextCurrent() const {
  return gl_context != nullptr && glXGetCurrentContext() == gl_context;
}

void GameWindow::shutdownGL(){
  if (!gl_context) return;
  if (gl_thread != std::this_thread::get_id())
    throw std::runtime_error("GL context must be shut down on its owning thread");
  if (glXGetCurrentContext() != gl_context)
    throw std::runtime_error("GL context is not current on its owning thread");
  if (!glXMakeCurrent(disp, None, nullptr))
    throw std::runtime_error("Unable to release GLX context");
  glXDestroyContext(disp, gl_context);
  gl_context = nullptr;
  gl_thread = {};
}

bool GameWindow::isPressed(char c, const std::bitset<256>& state, bool shift){
  KeySym symbol = c == ' ' ? XK_space : XStringToKeysym(std::string(1, c).c_str());
  KeySym lower = NoSymbol, upper = NoSymbol;
  XConvertCase(symbol, &lower, &upper);
  if (lower == NoSymbol) return false;
  Display *display = instance().disp;
  const KeyCode key = XKeysymToKeycode(display, lower);
  const KeyCode left = XKeysymToKeycode(display, XK_Shift_L);
  const KeyCode right = XKeysymToKeycode(display, XK_Shift_R);
  return key != 0 && state.test(key) &&
         (!shift || (left != 0 && state.test(left)) ||
                    (right != 0 && state.test(right)));
}

bool GameWindow::grabInput(){
  if(XGrabPointer(disp, window, True, PointerMotionMask | ButtonPressMask,
                  GrabModeAsync, GrabModeAsync, window, None, CurrentTime) != GrabSuccess){
    return false;
  }

  if(XGrabKeyboard(disp, window, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess){
    XUngrabPointer(disp, CurrentTime);
    XFlush(disp);
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
  XUngrabPointer(disp, CurrentTime);
  XUngrabKeyboard(disp, CurrentTime);
  XFlush(disp);
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
    XNextEvent(this->disp, &e); // blocking call

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
      if (!detectable_repeat && XEventsQueued(disp, QueuedAfterReading) > 0) {
        XEvent next;
        XPeekEvent(disp, &next);
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
