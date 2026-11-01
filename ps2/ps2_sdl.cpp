/* ps2_sdl.cpp: the SDL2 calls Descent 3 makes, on maxsdk, and main.

   Video: one window, the GS picture through libs/libgl (mxgl_window
   gives the game the size it asks for); SDL_GL_GetProcAddress is in
   ps2_gl.cpp. Input: pad 0, read in SDL_PumpEvents, becomes the keyboard
   and mouse events the game knows (its own controls then work as they are)
   and a joystick of 6 axes, 16 buttons and a hat. Audio: none yet (no
   device opens). Threads: none (SDL_CreateThread fails). */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include "SDL.h"
#include "SDL_thread.h"

extern "C" {
#include <mxgl.h>
#include <mx/debug.h>
#include <mx/pad.h>
#include <mx/vm.h>
void mx_exit(int status) __attribute__((noreturn));
}

#include "runcfg.h"

void ps2_frame_end(void); /* ps2_gl.cpp */

static char sdl_error[256];

extern "C" {

int SDL_SetError(SDL_PRINTF_FORMAT_STRING const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(sdl_error, sizeof sdl_error, fmt, ap);
  va_end(ap);
  return -1;
}
const char *SDL_GetError(void) { return sdl_error; }
void SDL_ClearError(void) { sdl_error[0] = 0; }

/* ---- init ------------------------------------------------------------------- */

static Uint32 inited;
static int pad_ok = -1;

int SDL_Init(Uint32 flags) { return SDL_InitSubSystem(flags); }
int SDL_InitSubSystem(Uint32 flags) {
  if ((flags & (SDL_INIT_JOYSTICK | SDL_INIT_VIDEO)) && pad_ok < 0) {
    pad_ok = mx_pad_init() >= 0;
    mx_printf("ps2: pad %s\n", pad_ok ? "ok" : "no driver");
  }
  if (flags & SDL_INIT_VIDEO)
    mxgl_init();
  inited |= flags;
  return 0;
}
void SDL_QuitSubSystem(Uint32 flags) { inited &= ~flags; }
Uint32 SDL_WasInit(Uint32 flags) { return inited & flags; }
void SDL_Quit(void) { inited = 0; }

char *SDL_GetPrefPath(const char *org, const char *app) {
  /* the game's own directory (pilots, the registry file) */
  char *p = (char *)malloc(PATH_MAX);
  if (!getcwd(p, PATH_MAX))
    strcpy(p, "/");
  size_t n = strlen(p);
  if (n && p[n - 1] != '/')
    strcat(p, "/");
  return p;
}

/* ---- video ------------------------------------------------------------------ */

struct SDL_Window {
  int w, h;
  Uint32 flags;
};
static SDL_Window the_window;

int SDL_GL_SetAttribute(SDL_GLattr attr, int value) { return 0; }
int SDL_GL_LoadLibrary(const char *path) { return 0; }
SDL_bool SDL_GL_ExtensionSupported(const char *ext) { return SDL_FALSE; }

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
  mxgl_init();
  the_window.w = w, the_window.h = h, the_window.flags = flags;
  mxgl_window(w, h);
  mx_printf("ps2: window %d x %d\n", w, h);
  return &the_window;
}
void SDL_SetWindowSize(SDL_Window *win, int w, int h) {
  win->w = w, win->h = h;
  mxgl_window(w, h);
}
int SDL_SetWindowFullscreen(SDL_Window *win, Uint32 flags) {
  win->flags = flags;
  return 0;
}
Uint32 SDL_GetWindowFlags(SDL_Window *win) { return win->flags | SDL_WINDOW_FULLSCREEN; }
void SDL_DestroyWindow(SDL_Window *win) {}
SDL_GLContext SDL_GL_CreateContext(SDL_Window *win) { return (SDL_GLContext)win; }
int SDL_GL_MakeCurrent(SDL_Window *win, SDL_GLContext ctx) { return 0; }
void SDL_GL_DeleteContext(SDL_GLContext ctx) {}
void SDL_GL_GetDrawableSize(SDL_Window *win, int *w, int *h) {
  *w = win->w;
  *h = win->h;
}
void SDL_CalculateGammaRamp(float gamma, Uint16 *ramp) {
  for (int i = 0; i < 256; i++)
    ramp[i] = (Uint16)(i * 257);
}
int SDL_SetWindowGammaRamp(SDL_Window *win, const Uint16 *r, const Uint16 *g, const Uint16 *b) {
  return -1;
}
int SDL_SetRelativeMouseMode(SDL_bool on) { return 0; }

void SDL_GL_SwapWindow(SDL_Window *win) { ps2_frame_end(); }

/* ---- events: the pad ---------------------------------------------------------- */

static SDL_EventFilter filter;
static void *filter_data;

void SDL_SetEventFilter(SDL_EventFilter f, void *data) {
  filter = f;
  filter_data = data;
}

static void post(SDL_Event *e) {
  if (filter)
    filter(filter_data, e);
}

static void key(SDL_Keycode sym, int down) {
  SDL_Event e;
  memset(&e, 0, sizeof e);
  e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.key.keysym.sym = sym;
  post(&e);
}

static int mouse_x = 320, mouse_y = 240;
static void mouse_button(int b, int down) {
  SDL_Event e;
  memset(&e, 0, sizeof e);
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  e.button.button = (Uint8)b;
  e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.button.x = mouse_x, e.button.y = mouse_y;
  post(&e);
}

/* the buttons as keys (Descent 3's default controls and its menus) and as
   the mouse's: cross Enter (menus) and fire primary (the mouse's left
   button: R1 too), circle Escape, square fire secondary (right button),
   triangle the next primary weapon, L1/L2 slide, the d-pad the arrows
   (menus; in flight pitch and heading), L3 afterburner, select the
   automap, start the menu (Escape) */
static const struct {
  u32 bit;
  SDL_Keycode sym; /* 0: none */
  int mouse;       /* 0: none */
} pad_map[] = {
    {MX_PAD_UP, SDLK_UP, 0},          {MX_PAD_DOWN, SDLK_DOWN, 0},
    {MX_PAD_LEFT, SDLK_LEFT, 0},      {MX_PAD_RIGHT, SDLK_RIGHT, 0},
    {MX_PAD_CROSS, SDLK_RETURN, 0},   {MX_PAD_CIRCLE, SDLK_ESCAPE, 0},
    {MX_PAD_R1, 0, SDL_BUTTON_LEFT},  {MX_PAD_R2, 0, SDL_BUTTON_RIGHT},
    {MX_PAD_SQUARE, 0, SDL_BUTTON_RIGHT}, {MX_PAD_TRIANGLE, SDLK_1, 0},
    {MX_PAD_L1, SDLK_KP_1, 0},        {MX_PAD_L2, SDLK_KP_3, 0},
    {MX_PAD_L3, SDLK_s, 0},           {MX_PAD_SELECT, SDLK_TAB, 0},
    {MX_PAD_START, SDLK_ESCAPE, 0},
};

static u32 pad_held;
static u8 pad_st[4] = {128, 128, 128, 128};

static int stick(int v) {
  v -= 128;
  return v > -24 && v < 24 ? 0 : v > 0 ? (v - 24) * 32767 / 103 : (v + 24) * 32768 / 104;
}

void SDL_PumpEvents(void) {
  if (pad_ok <= 0)
    return;
  u32 held = mx_pad_buttons(0);
  u32 change = held ^ pad_held;
  for (unsigned i = 0; i < sizeof pad_map / sizeof pad_map[0]; i++)
    if (change & pad_map[i].bit) {
      int down = (held & pad_map[i].bit) != 0;
      if (pad_map[i].sym)
        key(pad_map[i].sym, down);
      if (pad_map[i].mouse)
        mouse_button(pad_map[i].mouse, down);
    }
  pad_held = held;
  /* the left stick: forward and back (A, Z), the right stick: the mouse */
  static int fwd;
  mx_pad_sticks(0, pad_st);
  int ly = stick(pad_st[3]);
  int f = ly < -16000 ? 1 : ly > 16000 ? -1 : 0;
  if (f != fwd) {
    if (fwd)
      key(fwd > 0 ? SDLK_a : SDLK_z, 0);
    if (f)
      key(f > 0 ? SDLK_a : SDLK_z, 1);
    fwd = f;
  }
  int dx = stick(pad_st[0]) / 2048, dy = stick(pad_st[1]) / 2048;
  if (dx || dy) {
    mouse_x += dx, mouse_y += dy;
    int w = the_window.w ? the_window.w : 640, h = the_window.h ? the_window.h : 480;
    mouse_x = mouse_x < 0 ? 0 : mouse_x >= w ? w - 1 : mouse_x;
    mouse_y = mouse_y < 0 ? 0 : mouse_y >= h ? h - 1 : mouse_y;
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEMOTION;
    e.motion.x = mouse_x, e.motion.y = mouse_y;
    e.motion.xrel = dx, e.motion.yrel = dy;
    post(&e);
  }
}

/* ---- the joystick: pad 0 ------------------------------------------------------- */

struct _SDL_Joystick {
  int dummy;
};
static _SDL_Joystick the_stick;

int SDL_NumJoysticks(void) { return pad_ok > 0 ? 1 : 0; }
const char *SDL_JoystickNameForIndex(int i) { return "PS2 pad"; }
SDL_Joystick *SDL_JoystickOpen(int i) { return i == 0 ? &the_stick : NULL; }
void SDL_JoystickClose(SDL_Joystick *j) {}
int SDL_JoystickNumAxes(SDL_Joystick *j) { return 4; }
int SDL_JoystickNumButtons(SDL_Joystick *j) { return 16; }
int SDL_JoystickNumHats(SDL_Joystick *j) { return 1; }
/* axes: 0, 1 the left stick, 2, 3 the right */
Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis) {
  static const int st[4] = {2, 3, 0, 1};
  if (axis < 0 || axis > 3)
    return 0;
  int v = stick(pad_st[st[axis]]);
  return (Sint16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}
Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int b) {
  return b >= 0 && b < 16 && (pad_held & (1u << b)) ? 1 : 0;
}
Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int hat) {
  Uint8 h = SDL_HAT_CENTERED;
  if (pad_held & MX_PAD_UP)
    h |= SDL_HAT_UP;
  if (pad_held & MX_PAD_DOWN)
    h |= SDL_HAT_DOWN;
  if (pad_held & MX_PAD_LEFT)
    h |= SDL_HAT_LEFT;
  if (pad_held & MX_PAD_RIGHT)
    h |= SDL_HAT_RIGHT;
  return h;
}

/* ---- audio: none yet ---------------------------------------------------------- */

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *dev, int iscapture, const SDL_AudioSpec *want,
                                      SDL_AudioSpec *have, int changes) {
  SDL_SetError("no audio on the PS2 yet");
  return 0;
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) {}
void SDL_LockAudioDevice(SDL_AudioDeviceID dev) {}
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) {}
int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len) { return -1; }

/* ---- threads: none ------------------------------------------------------------ */

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data) {
  SDL_SetError("no threads");
  return NULL;
}
void SDL_WaitThread(SDL_Thread *t, int *status) {}

} /* extern "C" */

/* ---- main ---------------------------------------------------------------------- */

int d3_main(int argc, char *argv[]);

void ps2_end(int status) {
  mx_printf("@@PS2TEST_END@@ status=%d\n", status);
  mx_exit(status);
}

extern "C" void *mx_heap_start(void);
extern "C" void *mx_heap_end(void);

/* maxsdk's virtual memory (<mx/vm.h>): malloc in up to 128 MiB, paged to a
   swap file in the game's directory; the core heap (PS2_CORE_HEAP) holds
   what the DMAC reads: libgl's arenas and textures. Without TLB misses
   (PCSX2's recompiler) VM stays off and the game has the 32 MiB as before. */
extern "C" const struct mx_vm_config mx_vm_config = {
    .swap = "host:" PS2_SWAP,
    .core_heap = PS2_CORE_HEAP,
};

int main(int argc, char **argv) {
  if (mx_vm_active())
    mx_printf("Descent 3 on maxsdk: virtual memory %u MiB, core heap %u KiB\n", mx_vm_size() >> 20,
              PS2_CORE_HEAP >> 10);
  else
    mx_printf("Descent 3 on maxsdk: heap %u KiB\n",
              (unsigned)((char *)mx_heap_end() - (char *)mx_heap_start()) / 1024);
  /* the game's directory on the file server: "/d3" is host:d3 */
  if (chdir(PS2_DIR) < 0)
    mx_printf("ps2: no directory %s\n", PS2_DIR);
  static char line[] = "descent3 -setdir " PS2_DIR " -tempdir " PS2_DIR " -lowmem -nosound -nomusic " PS2_ARGS;
  static char *args[64];
  int n = 0;
  for (char *p = strtok(line, " "); p && n < 63; p = strtok(NULL, " "))
    args[n++] = p;
  args[n] = NULL;
  int r = d3_main(n, args);
  ps2_end(r);
}
