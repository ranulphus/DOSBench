/* scene.h - .DBS scene files (tools/dbs.py describes the format). */
#ifndef SCENE_H
#define SCENE_H
#include <stdint.h>
#include "rb.h"

enum { SC_TF_MIPMAP = 1, SC_TF_CLAMP = 2, SC_TF_LIGHTMAP = 4 };
enum { SC_BF_SKY = 1, SC_BF_TRANS = 2, SC_BF_ALPHATEST = 4, SC_BF_TWOSIDED = 8, SC_BF_SPRITE = 16 };
#define SC_NONE 0xFFFFu

typedef struct {
    uint16_t tex, lm, flags, leaf;
    uint32_t vfirst, vcount, ifirst, icount;
    float mins[3], maxs[3];
} sc_batch;

typedef struct {
    int w, h, flags;
    const uint8_t *rgba;
} sc_texture;

typedef struct {
    uint32_t kind;              /* 0 orbit, 1 path */
    uint32_t frames;            /* per revolution, or along the path */
    float znear, zfar;
    uint32_t fog_rgb;
    float fog_start, fog_end;   /* fog_end 0: no fog */
    uint32_t nkeys;
    const float *orbit;         /* centre[3], radius, height */
    const float *keys;          /* nkeys x (pos[3], look[3]) */
} sc_view;

typedef struct {
    uint8_t *file;
    long size;
    int ntex;
    sc_texture *tex;
    long nvert, nidx;
    const rb_vertex *vert;
    const uint16_t *idx;
    int nbatch;
    const sc_batch *batch;
    sc_view view;
    const uint8_t *vis;         /* VISL section, or NULL */
    long vis_size;
    const char *info;
    long info_size;
    /* Created by sc_upload. */
    rb_tex **rtex;
    rb_mesh **mesh;
    double tris;
} scene;

/* Parse a file already in memory (takes ownership of buf, freed by sc_free). */
int  sc_parse(scene *s, uint8_t *buf, long size, char *err, int errlen);
int  sc_load(scene *s, const char *path, char *err, int errlen);
/* Create the backend textures, and static meshes when meshes != 0. */
int  sc_upload(scene *s, int meshes);
void sc_free(scene *s);
/* The camera for frame f: eye and look-at point. */
void sc_camera(const scene *s, int f, float eye[3], float at[3]);
/* A value from INFO ("" if absent). */
const char *sc_info(const scene *s, const char *key, char *buf, int buflen);

#endif
