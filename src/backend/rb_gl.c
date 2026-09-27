/* rb_gl.c - the render backend on OpenGL 1.1 (DOS-GL).
 *
 * Geometry goes to GL in object space with the matrices loaded, so DOS-GL
 * does the transform, clipping and projection. Three submission paths, as
 * GL games used them: vertex arrays with glDrawElements (the default),
 * display lists compiled once per static mesh, and immediate mode. */
#include "rb.h"
#include "texutil.h"
#include <GL/gl.h>
#include <GL/dosgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rb_tex {
    GLuint name;
    int w, h, flags;
    int filt;                   /* filtering applied: 0 none, 1 point, 2 bilinear */
};

struct rb_mesh {
    rb_vertex *v;
    uint16_t *idx;
    int nv, ni;
    GLuint list[2];             /* per uv set, compiled on first use */
};

static rb_info info;
static int submit = RB_SUBMIT_ARRAYS;
static rb_state st;
static int st_valid, is_open;
static unsigned long tex_sent;
static uint8_t *rowbuf;
static rb_tex *cur;

static void filter(rb_tex *t);

int rb_load(const char *path, char *err, int errlen)
{
    (void)path; (void)err; (void)errlen;
    return 0;
}

int rb_open(int w, int h, int vsync, char *err, int errlen)
{
    DGLConfig cfg;
    const DGLDeviceInfo *d;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = w;
    cfg.height = h;
    cfg.color_bits = 16;
    cfg.depth_bits = 16;
    cfg.double_buffer = 1;
    cfg.vsync = vsync;
    if (dglInit(&cfg) != 0) {
        snprintf(err, (size_t)errlen, "dglInit %dx%d: %s", w, h, dglGetErrorString());
        return -1;
    }
    is_open = 1;
    d = dglGetDeviceInfo();
    memset(&info, 0, sizeof info);
    info.api = "opengl";
    snprintf(info.impl, sizeof info.impl, "%s", dglVersion());
    snprintf(info.card, sizeof info.card, "%s rev%u %luMB", d->chip_name, d->revision, d->vram_bytes >> 20);
    info.width = w;
    info.height = h;
    info.tex_mem = d->vram_bytes;
    {
        GLint m = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m);
        info.max_tex = m > 256 ? 256 : m;
    }
    glViewport(0, 0, w, h);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDepthFunc(GL_LEQUAL);
    glAlphaFunc(GL_GREATER, 0.5f);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glShadeModel(GL_SMOOTH);
    st_valid = 0;
    {
        rb_state s;
        memset(&s, 0, sizeof s);
        rb_set_state(&s);
    }
    return 0;
}

void rb_close(void)
{
    if (is_open)
        dglShutdown();
    is_open = 0;
    free(rowbuf);
    rowbuf = NULL;
}

const rb_info *rb_get_info(void) { return &info; }
void rb_set_submit(int mode) { submit = mode; }
int rb_get_submit(void) { return submit; }
unsigned long rb_tex_bytes(void) { return tex_sent; }

void rb_clear(uint32_t rgb)
{
    glClearColor(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1.0f);
    if (!st.depth_write)                /* glClear honours the depth mask */
        glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!st.depth_write)
        glDepthMask(GL_FALSE);
}

static void enable(GLenum cap, int on)
{
    if (on)
        glEnable(cap);
    else
        glDisable(cap);
}

void rb_set_state(const rb_state *s)
{
    int all = !st_valid;
    if (all || s->depth_test != st.depth_test || s->depth_write != st.depth_write) {
        /* GL skips depth writes when the test is off: always pass instead. */
        enable(GL_DEPTH_TEST, s->depth_test || s->depth_write);
        glDepthFunc(s->depth_test ? GL_LEQUAL : GL_ALWAYS);
        glDepthMask(s->depth_write ? GL_TRUE : GL_FALSE);
    }
    if (all || s->blend != st.blend) {
        enable(GL_BLEND, s->blend != RB_BLEND_NONE);
        switch (s->blend) {
        case RB_BLEND_ALPHA: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
        case RB_BLEND_ADD: glBlendFunc(GL_ONE, GL_ONE); break;
        case RB_BLEND_MUL: glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
        default: break;
        }
    }
    if (all || s->alpha_test != st.alpha_test)
        enable(GL_ALPHA_TEST, s->alpha_test);
    if (all || s->fog != st.fog || (s->fog && (s->fog_start != st.fog_start || s->fog_end != st.fog_end ||
                                               s->fog_rgb != st.fog_rgb))) {
        enable(GL_FOG, s->fog);
        if (s->fog) {
            GLfloat c[4];
            c[0] = ((s->fog_rgb >> 16) & 255) / 255.0f;
            c[1] = ((s->fog_rgb >> 8) & 255) / 255.0f;
            c[2] = (s->fog_rgb & 255) / 255.0f;
            c[3] = 1.0f;
            glFogfv(GL_FOG_COLOR, c);
            glFogf(GL_FOG_START, s->fog_start);
            glFogf(GL_FOG_END, s->fog_end);
        }
    }
    if (all || s->tex != st.tex) {
        enable(GL_TEXTURE_2D, s->tex != RB_TEX_OFF);
        if (s->tex != RB_TEX_OFF)
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, s->tex == RB_TEX_REPLACE ? GL_REPLACE : GL_MODULATE);
    }
    if (all || s->cull != st.cull)
        enable(GL_CULL_FACE, s->cull);
    st = *s;
    st_valid = 1;
    if (cur)                            /* filtering is per texture object in GL */
        filter(cur);
}

void rb_set_matrices(const mat4 *proj, const mat4 *mv)
{
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj->m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(mv->m);
}

/* ---- Textures -------------------------------------------------------- */
static void upload(rb_tex *t, const uint8_t *rgba, int sub)
{
    uint8_t *lv = NULL, *nx;
    const uint8_t *src = rgba;
    int w = t->w, h = t->h, levels = t->flags & RB_TF_MIPMAP ? tu_levels(w, h) : 1, l;
    for (l = 0; l < levels; l++) {
        if (sub)
            glTexSubImage2D(GL_TEXTURE_2D, l, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, src);
        else
            glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
        tex_sent += (unsigned long)w * h * 2;
        if (l + 1 < levels) {
            nx = (uint8_t *)malloc((size_t)(w > 1 ? w / 2 : 1) * (h > 1 ? h / 2 : 1) * 4);
            if (!nx)
                break;
            tu_mip(src, w, h, nx);
            free(lv);
            src = lv = nx;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    free(lv);
}

static void filter(rb_tex *t)
{
    GLint mag = st.bilinear ? GL_LINEAR : GL_NEAREST, min = mag;
    if (t->filt == 1 + (st.bilinear != 0))
        return;
    t->filt = 1 + (st.bilinear != 0);
    if (t->flags & RB_TF_MIPMAP)
        min = st.bilinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
}

rb_tex *rb_tex_create(int w, int h, const uint8_t *rgba, int flags)
{
    rb_tex *t;
    GLint wrap = flags & RB_TF_CLAMP ? GL_CLAMP : GL_REPEAT;
    if (!tu_is_pow2(w) || !tu_is_pow2(h) || w > 256 || h > 256)
        return NULL;
    t = (rb_tex *)calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->w = w;
    t->h = h;
    t->flags = flags;
    glGenTextures(1, &t->name);
    glBindTexture(GL_TEXTURE_2D, t->name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    filter(t);
    upload(t, rgba, 0);
    cur = t;
    return t;
}

void rb_tex_update(rb_tex *t, const uint8_t *rgba)
{
    glBindTexture(GL_TEXTURE_2D, t->name);
    upload(t, rgba, 1);
    cur = t;
}

void rb_tex_bind(rb_tex *t)
{
    cur = t;
    glBindTexture(GL_TEXTURE_2D, t ? t->name : 0);
    if (t)
        filter(t);
}

void rb_tex_free(rb_tex *t)
{
    if (!t)
        return;
    if (cur == t)
        cur = NULL;
    glDeleteTextures(1, &t->name);
    free(t);
}

/* ---- Geometry -------------------------------------------------------- */
static void pointers(const rb_vertex *v)
{
    glVertexPointer(3, GL_FLOAT, sizeof *v, &v->x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof *v, v->c);
    glTexCoordPointer(2, GL_FLOAT, sizeof *v, st.uvset ? &v->u2 : &v->u);
}

static void immediate(const rb_vertex *v, const uint16_t *idx, int ni)
{
    int i;
    glBegin(GL_TRIANGLES);
    for (i = 0; i < ni; i++) {
        const rb_vertex *p = &v[idx[i]];
        glColor4ub(p->c[0], p->c[1], p->c[2], p->c[3]);
        if (st.uvset)
            glTexCoord2f(p->u2, p->v2);
        else
            glTexCoord2f(p->u, p->v);
        glVertex3f(p->x, p->y, p->z);
    }
    glEnd();
}

void rb_draw(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    (void)nv;
    if (submit == RB_SUBMIT_IMMEDIATE) {
        immediate(v, idx, ni);
        return;
    }
    pointers(v);
    glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, idx);
}

rb_mesh *rb_mesh_create(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    rb_mesh *m = (rb_mesh *)calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->v = (rb_vertex *)malloc((size_t)nv * sizeof *v);
    m->idx = (uint16_t *)malloc((size_t)ni * sizeof *idx);
    if (!m->v || !m->idx) {
        rb_mesh_free(m);
        return NULL;
    }
    memcpy(m->v, v, (size_t)nv * sizeof *v);
    memcpy(m->idx, idx, (size_t)ni * sizeof *idx);
    m->nv = nv;
    m->ni = ni;
    return m;
}

void rb_mesh_draw(rb_mesh *m)
{
    if (submit == RB_SUBMIT_LISTS) {
        GLuint *l = &m->list[st.uvset ? 1 : 0];
        if (!*l) {
            *l = glGenLists(1);
            pointers(m->v);
            glNewList(*l, GL_COMPILE);
            glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, m->idx);
            glEndList();
        }
        glCallList(*l);
        return;
    }
    rb_draw(m->v, m->nv, m->idx, m->ni);
}

void rb_mesh_free(rb_mesh *m)
{
    if (!m)
        return;
    if (m->list[0])
        glDeleteLists(m->list[0], 1);
    if (m->list[1])
        glDeleteLists(m->list[1], 1);
    free(m->v);
    free(m->idx);
    free(m);
}

void rb_swap(void)
{
    dglSwapBuffers();
}

void rb_finish(void)
{
    glFinish();
}

int rb_read(uint8_t *rgb)
{
    int w = info.width, h = info.height, y;
    long row = (long)w * 3;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    if (glGetError() != GL_NO_ERROR)
        return -1;
    if (!rowbuf)
        rowbuf = (uint8_t *)malloc((size_t)row);
    if (!rowbuf)
        return -1;
    for (y = 0; y < h / 2; y++) {           /* GL rows are bottom-up */
        memcpy(rowbuf, rgb + y * row, (size_t)row);
        memcpy(rgb + y * row, rgb + (h - 1 - y) * row, (size_t)row);
        memcpy(rgb + (h - 1 - y) * row, rowbuf, (size_t)row);
    }
    return 0;
}
