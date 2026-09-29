/* rb.h - the render backend: the one interface the benchmark core draws
 * through. rb_glide.c implements it on Glide 2.x (CPU transform, clipping
 * and projection, as Glide games did); rb_gl.c on OpenGL 1.1 (DOS-GL takes
 * object-space arrays, display lists or immediate mode). */
#ifndef RB_H
#define RB_H
#include <stdint.h>
#include "vmath.h"

/* 32 bytes; the layout of .DBS vertex records. */
typedef struct {
    float x, y, z;
    uint8_t c[4];               /* r, g, b, a */
    float u, v;                 /* texture */
    float u2, v2;               /* lightmap */
} rb_vertex;

enum { RB_BLEND_NONE, RB_BLEND_ALPHA, RB_BLEND_ADD, RB_BLEND_MUL };
enum { RB_TEX_OFF, RB_TEX_MODULATE, RB_TEX_REPLACE };
enum { RB_SUBMIT_ARRAYS, RB_SUBMIT_LISTS, RB_SUBMIT_IMMEDIATE };

typedef struct {
    int depth_test, depth_write;
    int blend;                  /* RB_BLEND_* */
    int alpha_test;             /* pass when alpha > 0.5 */
    int fog;                    /* linear in eye distance */
    float fog_start, fog_end;
    uint32_t fog_rgb;           /* 0xRRGGBB */
    int tex;                    /* RB_TEX_* */
    int bilinear;
    int cull;                   /* cull back faces (front = counter-clockwise) */
    int uvset;                  /* 0: u,v  1: u2,v2 */
} rb_state;

typedef struct {
    const char *api;            /* "glide" or "opengl" */
    char impl[64];              /* runtime or library and its version */
    char card[40];              /* what the implementation reports */
    int width, height;
    unsigned long tex_mem;      /* bytes available to textures */
    int max_tex;                /* largest texture side */
} rb_info;

enum { RB_TF_MIPMAP = 1, RB_TF_CLAMP = 2 };

typedef struct rb_tex rb_tex;
typedef struct rb_mesh rb_mesh;

/* Load the implementation: the Glide backend loads the OVL at 'path' (NULL
 * for C:\TEST\GLIDE2X.OVL, then GLIDE2X.OVL); OpenGL has nothing to load. */
int  rb_load(const char *path, char *err, int errlen);
int  rb_open(int width, int height, int vsync, char *err, int errlen);
void rb_close(void);
const rb_info *rb_get_info(void);
void rb_set_submit(int mode);           /* RB_SUBMIT_*; the Glide backend has one path */
int  rb_get_submit(void);

void rb_clear(uint32_t rgb);            /* colour and depth */
void rb_set_state(const rb_state *s);
void rb_set_matrices(const mat4 *proj, const mat4 *modelview);

/* Textures are RGBA8, power-of-two sides up to 256, aspect up to 8:1. */
rb_tex *rb_tex_create(int w, int h, const uint8_t *rgba, int flags);
void rb_tex_update(rb_tex *t, const uint8_t *rgba);
/* Replace a w x h rectangle at (x, y) of level 0 (rgba holds just that
 * rectangle). OpenGL only: tests using it are T_GL_ONLY. */
void rb_tex_update_rect(rb_tex *t, int x, int y, int w, int h, const uint8_t *rgba);
void rb_tex_bind(rb_tex *t);            /* NULL unbinds */
void rb_tex_free(rb_tex *t);

/* Indexed triangle list; indices address v[0..nv). */
void rb_draw(const rb_vertex *v, int nv, const uint16_t *idx, int ni);
/* Static geometry: the arrays are copied. */
rb_mesh *rb_mesh_create(const rb_vertex *v, int nv, const uint16_t *idx, int ni);
void rb_mesh_draw(rb_mesh *m);
void rb_mesh_free(rb_mesh *m);

void rb_swap(void);
void rb_finish(void);                   /* wait until the hardware is idle */
/* Read the frame being drawn (before rb_swap) as RGB888, top row first. */
int  rb_read(uint8_t *rgb);
/* Bytes of texture data sent to the hardware so far. */
unsigned long rb_tex_bytes(void);

#endif
