/* ps2_gl.cpp: the GL that Descent 3's renderer (renderer/HardwareOpenGL.cpp)
   finds through SDL_GL_GetProcAddress, on libs/libgl.

   The GS has one texture unit and libgl draws with it only; the game
   sees two (ARB_multitexture): the calls that name a texture unit are kept
   here, and libgl always has the texture of the active unit bound.
   Textures made while unit 1 is active are lightmaps: kept as their
   brightness (GL_LUMINANCE8; the small ones in atlas pages), drawn by
   the polygon's second pass, blended DST_COLOR, ZERO (the GS's Cd * As).

   The renderer's polygons (gpu_RenderPolygon, gpu_RenderPolygonUV2) come
   here (ps2_render_polygon): libgl takes s, t (the game's s/w, t/w)
   and colours as bytes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_opengl.h"

#include "pserror.h"
#include "3d.h"
#include "HardwareInternal.h"

extern "C" {
#include <mxgl.h>
#include <mx/debug.h>
#include <mx/mx.h>
}

#include "runcfg.h"

void ps2_end(int status);
extern "C" void ps2_mem_report(const char *when); /* ps2_mem.c */

/* ---- textures: the game's names ------------------------------------------------ */

/* The game names its textures itself (1, 2, ...: tens of thousands with a
   level's lightmaps); libgl has MAX_TEX (2048). A game name is a
   libgl texture of its own, or, for a lightmap of up to 64 x 64, a slot
   in an atlas page: a 128 x 128 brightness texture of slots of one size (the
   lightmap's and a border of one texel, its edge again, against the bilinear
   filter's bleeding). */
#define PAGE 128
#define MAX_PAGES 160
#define MAX_SLOTS 1024 /* (128 / 4)^2: 2 x 2 lightmaps */

struct gtex {
  uint16_t mx;   /* libgl's texture (the page's for a lightmap in one) */
  uint8_t atlas; /* page + 1, 0: a texture of its own */
  uint8_t x, y;  /* its slot in the page (the border's corner) */
  uint8_t size;
};
static gtex *gt;
static unsigned ngt;

static struct page {
  GLuint mx;
  int size, per_row, count;
  uint8_t used[MAX_SLOTS / 8];
} pages[MAX_PAGES];

static GLuint mx_bound; /* libgl's */

static void bind(GLuint t) {
  if (t != mx_bound) {
    mx_bound = t;
    glBindTexture(GL_TEXTURE_2D, t);
  }
}

static gtex *gtex_of(GLuint n) {
  if (n >= ngt) {
    unsigned nn = (n + 1024) & ~1023u;
    gtex *g = (gtex *)realloc(gt, nn * sizeof *g);
    if (!g)
      return NULL;
    memset(g + ngt, 0, (nn - ngt) * sizeof *g);
    gt = g, ngt = nn;
  }
  return &gt[n];
}

/* the libgl texture of game name n (made for a name new to it) */
static GLuint mx_of(GLuint n) {
  if (!n)
    return 0;
  gtex *g = gtex_of(n);
  if (!g)
    return 0;
  if (!g->mx && !g->atlas) {
    GLuint m = 0;
    glGenTextures(1, &m);
    if (!m)
      mx_printf("ps2: out of libgl textures (game texture %u)\n", n);
    g->mx = (uint16_t)m;
  }
  return g->mx;
}

/* a slot for a lightmap of size x size: returns its page (x, y: the slot's
   corner), -1 if there is none */
static int atlas_place(int size, int *x, int *y) {
  int slot = size + 2, per_row = PAGE / slot, n = per_row * per_row;
  int empty = -1;
  for (int i = 0; i < MAX_PAGES; i++) {
    struct page *pg = &pages[i];
    if (!pg->mx) {
      if (empty < 0)
        empty = i;
      continue;
    }
    if (pg->size != size || pg->count == n)
      continue;
    for (int k = 0; k < n; k++)
      if (!(pg->used[k >> 3] & (1 << (k & 7)))) {
        pg->used[k >> 3] |= (uint8_t)(1 << (k & 7));
        pg->count++;
        *x = k % per_row * slot, *y = k / per_row * slot;
        return i;
      }
  }
  if (empty < 0)
    return -1;
  struct page *pg = &pages[empty];
  GLuint m = 0;
  glGenTextures(1, &m);
  if (!m)
    return -1;
  memset(pg, 0, sizeof *pg);
  pg->mx = m, pg->size = size, pg->per_row = per_row, pg->count = 1;
  pg->used[0] = 1;
  bind(m);
  /* GL_LUMINANCE8: kept as its brightness by libgl */
  glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, PAGE, PAGE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  *x = *y = 0;
  return empty;
}

static void atlas_free(gtex *g) {
  struct page *pg = &pages[g->atlas - 1];
  int slot = pg->size + 2, k = g->y / slot * pg->per_row + g->x / slot;
  pg->used[k >> 3] &= (uint8_t)~(1 << (k & 7));
  if (--pg->count == 0) { /* the page goes when its last lightmap goes */
    GLuint m = pg->mx;
    if (m == mx_bound)
      mx_bound = (GLuint)-1;
    glDeleteTextures(1, &m);
    pg->mx = 0;
  }
}

/* a lightmap's texels (size x size, RGBA) into its place, with the border */
static void atlas_write(gtex *g, const GLvoid *px) {
  static uint32_t buf[(64 + 2) * (64 + 2)];
  const uint32_t *src = (const uint32_t *)px;
  int s = g->size, b = s + 2;
  for (int y = 0; y < b; y++) {
    int sy = y == 0 ? 0 : y > s ? s - 1 : y - 1;
    for (int x = 0; x < b; x++) {
      int sx = x == 0 ? 0 : x > s ? s - 1 : x - 1;
      buf[y * b + x] = src[sy * s + sx];
    }
  }
  bind(g->mx);
  glTexSubImage2D(GL_TEXTURE_2D, 0, g->x, g->y, b, b, GL_RGBA, GL_UNSIGNED_BYTE, buf);
}

/* ---- the texture units --------------------------------------------------------- */

static int unit, cunit;
static GLuint bound[2]; /* the game's names */
static int tex_on[2];
static int carray[2] = {0, 0}; /* texture coordinate arrays on */
static int blend_on;
static GLenum sfac = GL_ONE, dfac = GL_ZERO, dfunc = GL_LESS;
static GLboolean dmask = GL_TRUE;

static void APIENTRY ps2_ActiveTexture(GLenum t) { unit = t == GL_TEXTURE1_ARB; }
static void APIENTRY ps2_ClientActiveTexture(GLenum t) { cunit = t == GL_TEXTURE1_ARB; }
static void APIENTRY ps2_MultiTexCoord4f(GLenum t, GLfloat s, GLfloat tt, GLfloat r, GLfloat q) {}

static void APIENTRY ps2_BindTexture(GLenum target, GLuint t) { bound[unit] = t; }

/* the two texture units are here (the lightmap pass) */
static const GLubyte *APIENTRY ps2_GetString(GLenum name) {
  if (name == GL_EXTENSIONS)
    return (const GLubyte *)"GL_ARB_multitexture";
  return glGetString(name);
}

static void APIENTRY ps2_DeleteTextures(GLsizei n, const GLuint *t) {
  for (GLsizei i = 0; i < n; i++) {
    if (!t[i] || t[i] >= ngt)
      continue;
    gtex *g = &gt[t[i]];
    if (g->atlas) {
      atlas_free(g);
    } else if (g->mx) {
      GLuint m = g->mx;
      if (m == mx_bound)
        mx_bound = (GLuint)-1; /* bound again next time (libgl makes it anew) */
      glDeleteTextures(1, &m);
    }
    memset(g, 0, sizeof *g);
  }
}

static void APIENTRY ps2_TexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h,
                                    GLint border, GLenum fmt, GLenum type, const GLvoid *px) {
  if (level != 0 || !bound[unit])
    return;
  gtex *g = gtex_of(bound[unit]);
  if (!g)
    return;
  if (unit == 1 && w == h && w <= 64 && px && fmt == GL_RGBA) {
    if (!g->atlas || g->size != w) {
      if (g->atlas || g->mx)
        ps2_DeleteTextures(1, &bound[unit]);
      int x, y, p = atlas_place(w, &x, &y);
      if (p >= 0) {
        g->atlas = (uint8_t)(p + 1), g->mx = (uint16_t)pages[p].mx;
        g->x = (uint8_t)x, g->y = (uint8_t)y, g->size = (uint8_t)w;
      }
    }
    if (g->atlas) {
      atlas_write(g, px);
      return;
    }
  }
  bind(mx_of(bound[unit]));
  /* the lightmaps (unit 1) as their brightness: GL_LUMINANCE8 */
  glTexImage2D(target, level, unit == 1 ? GL_LUMINANCE8 : ifmt, w, h, border, fmt, type, px);
}

static void APIENTRY ps2_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                                       GLsizei h, GLenum fmt, GLenum type, const GLvoid *px) {
  if (!bound[unit])
    return;
  gtex *g = gtex_of(bound[unit]);
  if (g && g->atlas) {
    if (x == 0 && y == 0 && w == g->size && h == g->size)
      atlas_write(g, px);
    return;
  }
  bind(mx_of(bound[unit]));
  glTexSubImage2D(target, level, x, y, w, h, fmt, type, px);
}

static void APIENTRY ps2_TexParameteri(GLenum target, GLenum p, GLint v) {
  if (!bound[unit])
    return;
  gtex *g = gtex_of(bound[unit]);
  if (g && g->atlas)
    return; /* the page's: clamped, bilinear */
  bind(mx_of(bound[unit]));
  glTexParameteri(target, p, v);
}
static void APIENTRY ps2_TexEnvf(GLenum t, GLenum p, GLfloat v) {
  if (unit == 0)
    glTexEnvf(t, p, v);
}
static void set(GLenum cap, int on) {
  if (cap == GL_TEXTURE_2D) {
    tex_on[unit] = on;
    if (unit)
      return;
  }
  if (cap == GL_BLEND)
    blend_on = on;
  if (on)
    glEnable(cap);
  else
    glDisable(cap);
}
static void APIENTRY ps2_Enable(GLenum cap) { set(cap, 1); }
static void APIENTRY ps2_Disable(GLenum cap) { set(cap, 0); }
static void APIENTRY ps2_EnableClientState(GLenum a) {
  if (a == GL_TEXTURE_COORD_ARRAY)
    carray[cunit] = 1;
}
static void APIENTRY ps2_DisableClientState(GLenum a) {
  if (a == GL_TEXTURE_COORD_ARRAY)
    carray[cunit] = 0;
}
static void APIENTRY ps2_BlendFunc(GLenum s, GLenum d) {
  sfac = s, dfac = d;
  glBlendFunc(s, d);
}
static void APIENTRY ps2_DepthMask(GLboolean m) {
  dmask = m;
  glDepthMask(m);
}
static void APIENTRY ps2_DepthFunc(GLenum f) {
  dfunc = f;
  glDepthFunc(f);
}
/* the renderer's arrays are set by ps2_render_polygon */
static void APIENTRY ps2_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {}
static void APIENTRY ps2_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {}
static void APIENTRY ps2_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {}
static void APIENTRY ps2_NormalPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) {}

/* not on the GS (yet): fog, pixel rectangles */
static void APIENTRY ps2_Fogf(GLenum p, GLfloat v) {}
static void APIENTRY ps2_Fogfv(GLenum p, const GLfloat *v) {}
static void APIENTRY ps2_Fogi(GLenum p, GLint v) {}
static void APIENTRY ps2_DrawPixels(GLsizei w, GLsizei h, GLenum f, GLenum t, const GLvoid *px) {}
static void APIENTRY ps2_PixelTransferi(GLenum p, GLint v) {}
static void APIENTRY ps2_TexCoord4fv(const GLfloat *v) { glTexCoord2f(v[0] / v[3], v[1] / v[3]); }

/* ---- the polygons ------------------------------------------------------------ */

struct ps2_vert {
  float x, y, z, s, t;
};
static ps2_vert verts[128];
static GLuint cols[128];

static GLuint col_byte(float c) { return c <= 0 ? 0 : c >= 1 ? 255 : (GLuint)(c * 255.0f + 0.5f); }

static void draw(int n) {
  glVertexPointer(3, GL_FLOAT, sizeof verts[0], &verts[0].x);
  glTexCoordPointer(2, GL_FLOAT, sizeof verts[0], &verts[0].s);
  glColorPointer(4, GL_UNSIGNED_BYTE, 4, cols);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDrawArrays(GL_POLYGON, 0, n);
}

/* nv vertices of pos, col, uv0 (and uv1 for the lightmap pass if not NULL),
   stride bytes apart */
static void render(const vector *pos, const color_array *col, const tex_array *uv0,
                   const tex_array *uv1, int stride, uint32_t nv) {
  if (nv > 128)
    nv = 128;
  const char *pp = (const char *)pos, *cp = (const char *)col, *tp = (const char *)uv0;
  for (uint32_t i = 0; i < nv; i++, pp += stride, cp += stride, tp += stride) {
    const vector *p = (const vector *)pp;
    const color_array *c = (const color_array *)cp;
    const tex_array *t = (const tex_array *)tp;
    verts[i].x = p->x, verts[i].y = p->y, verts[i].z = p->z;
    float q = t->w != 0 ? 1.0f / t->w : 1.0f;
    verts[i].s = t->s * q, verts[i].t = t->t * q;
    cols[i] = col_byte(c->r) | col_byte(c->g) << 8 | col_byte(c->b) << 16 | col_byte(c->a) << 24;
  }
  bind(mx_of(bound[0]));
  if (!carray[0] && tex_on[0])
    glDisable(GL_TEXTURE_2D);
  draw((int)nv);
  if (!carray[0] && tex_on[0])
    glEnable(GL_TEXTURE_2D);

  if (uv1 && tex_on[1] && bound[1]) {
    gtex *g = gtex_of(bound[1]);
    /* a lightmap in a page: its s, t into the page's */
    float scale = 1.0f, ox = 0.0f, oy = 0.0f;
    if (g && g->atlas) {
      scale = g->size * (1.0f / PAGE);
      ox = (g->x + 1) * (1.0f / PAGE), oy = (g->y + 1) * (1.0f / PAGE);
    }
    tp = (const char *)uv1;
    for (uint32_t i = 0; i < nv; i++, tp += stride) {
      const tex_array *t = (const tex_array *)tp;
      float q = t->w != 0 ? 1.0f / t->w : 1.0f;
      verts[i].s = ox + t->s * q * scale, verts[i].t = oy + t->t * q * scale;
      cols[i] = 0xffffffff;
    }
    bind(g && g->atlas ? g->mx : mx_of(bound[1]));
    if (!tex_on[0])
      glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_DST_COLOR, GL_ZERO);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    draw((int)nv);
    if (!blend_on)
      glDisable(GL_BLEND);
    if (!tex_on[0])
      glDisable(GL_TEXTURE_2D);
    glBlendFunc(sfac, dfac);
    glDepthMask(dmask);
    glDepthFunc(dfunc);
  }
}

void ps2_render_polygon(PosColorUVVertex *v, uint32_t nv) {
  render(&v->pos, &v->color, &v->uv, NULL, sizeof *v, nv);
}
void ps2_render_polygon_uv2(PosColorUV2Vertex *v, uint32_t nv) {
  render(&v->pos, &v->color, &v->uv0, &v->uv1, sizeof *v, nv);
}

/* ---- the end of a frame -------------------------------------------------------- */

#include "gamesequence.h"

void ps2_frame_end(void) {
  static unsigned frame;
  mxgl_end_frame();
  mxgl_frame_done();
  /* the counted frames' time (EE cycles, added up: the counter wraps) */
  static unsigned long long cycles;
  static unsigned last;
  unsigned now = mx_count();
  if (!PS2_PLAY || GetGameState() == GAMESTATE_LVLPLAYING) {
    if (frame)
      cycles += now - last;
    frame++;
  }
  last = now;
  static unsigned said;
  if (frame % 60 == 0 && frame != said) {
    said = frame;
    mx_printf("ps2: frame %u\n", frame);
  }
  if (PS2_FRAMES && frame == PS2_FRAMES) {
    double s = (double)cycles / MX_EE_HZ;
    mx_printf("ps2: %u frames, %.1f seconds: %.1f fps\n", frame - 1, s, s > 0 ? (frame - 1) / s : 0.0);
    int r = mxgl_screenshot(PS2_SHOT);
    mx_printf("ps2: %u frames, picture %s: %d\n", frame, PS2_SHOT, r);
    ps2_mem_report("end");
    ps2_end(0);
  }
}

/* ---- SDL_GL_GetProcAddress ----------------------------------------------------- */

#define P(name) {#name, (void *)name},
static const struct {
  const char *name;
  void *fn;
} procs[] = {
    P(glAlphaFunc) P(glBegin) P(glClear) P(glClearColor) P(glClearDepth) P(glColor3ub)
    P(glColor4ub) P(glColor4f) P(glDepthRange) P(glDrawElements) P(glEnd)
    P(glFlush) P(glGetError) P(glHint) P(glLoadIdentity) P(glLoadMatrixf)
    P(glMatrixMode) P(glOrtho) P(glPixelStorei) P(glPolygonOffset) P(glReadPixels) P(glScissor)
    P(glShadeModel) P(glVertex2i) P(glVertex3f)
    P(glViewport) P(glColor4fv) P(glVertex3fv) P(glDrawArrays)
    {"glBindTexture", (void *)ps2_BindTexture},
    {"glTexSubImage2D", (void *)ps2_TexSubImage2D},
    {"glTexParameteri", (void *)ps2_TexParameteri},
    {"glGetString", (void *)ps2_GetString},
    {"glDeleteTextures", (void *)ps2_DeleteTextures},
    {"glTexImage2D", (void *)ps2_TexImage2D},
    {"glTexEnvf", (void *)ps2_TexEnvf},
    {"glEnable", (void *)ps2_Enable},
    {"glDisable", (void *)ps2_Disable},
    {"glEnableClientState", (void *)ps2_EnableClientState},
    {"glDisableClientState", (void *)ps2_DisableClientState},
    {"glBlendFunc", (void *)ps2_BlendFunc},
    {"glDepthMask", (void *)ps2_DepthMask},
    {"glDepthFunc", (void *)ps2_DepthFunc},
    {"glVertexPointer", (void *)ps2_VertexPointer},
    {"glColorPointer", (void *)ps2_ColorPointer},
    {"glTexCoordPointer", (void *)ps2_TexCoordPointer},
    {"glNormalPointer", (void *)ps2_NormalPointer},
    {"glFogf", (void *)ps2_Fogf},
    {"glFogfv", (void *)ps2_Fogfv},
    {"glFogi", (void *)ps2_Fogi},
    {"glDrawPixels", (void *)ps2_DrawPixels},
    {"glPixelTransferi", (void *)ps2_PixelTransferi},
    {"glTexCoord4fv", (void *)ps2_TexCoord4fv},
    {"glActiveTextureARB", (void *)ps2_ActiveTexture},
    {"glClientActiveTextureARB", (void *)ps2_ClientActiveTexture},
    {"glMultiTexCoord4f", (void *)ps2_MultiTexCoord4f},
};

extern "C" void *SDL_GL_GetProcAddress(const char *name) {
  for (unsigned i = 0; i < sizeof procs / sizeof procs[0]; i++)
    if (!strcmp(procs[i].name, name))
      return procs[i].fn;
  mx_printf("ps2: no GL function %s\n", name);
  return NULL;
}
