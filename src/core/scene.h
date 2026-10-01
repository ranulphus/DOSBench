/* scene.h - .DBS scene files (tools/dbs.py describes the format). */
#ifndef SCENE_H
#define SCENE_H
#include <stdint.h>
#include "rb.h"

enum { SC_TF_MIPMAP = 1, SC_TF_CLAMP = 2, SC_TF_LIGHTMAP = 4 };
enum { SC_BF_SKY = 1, SC_BF_TRANS = 2, SC_BF_ALPHATEST = 4, SC_BF_TWOSIDED = 8, SC_BF_SPRITE = 16,
       SC_BF_ADD = 32,                  /* additive, depth writes off, after the blended ones */
       SC_BF_GLOW = 64,                 /* additive and never fogged (lights at night) */
       SC_BF_DECAL = 128 };             /* on a surface: depth writes off, after the opaque ones */
#define SC_NONE 0xFFFFu

/* ---- Version 2: game scenes (tools/dbs.py describes the sections) ---- */
enum { SC_GF_FOG = 1 };
typedef struct {                        /* GHDR */
    uint32_t frames;
    float rate;                         /* story frames per second (25) */
    uint32_t clear_rgb, flags;          /* SC_GF_* */
    float fovy, znear, zfar;
    uint32_t fog_rgb;
    float fog_start, fog_end;
    float sun[3];                       /* towards the sun, unit length */
    float sun_rgb[3], ambient[3];       /* light colours, 1 = full */
    uint32_t seed;
    uint32_t sky;                       /* model drawn around the camera, or SC_NONE */
    uint32_t capture;                   /* the frame saved for image checks */
} sc_ghdr;

enum { SC_MF_LIT = 1, SC_MF_ANIM = 2 };
typedef struct {                        /* MODL: batches drawn together, model space */
    uint16_t batch0, nbatch, flags, lod_next;
    float lod_dist;                     /* beyond it (eye to centre), lod_next is drawn */
    float centre[3], radius;
    uint16_t anim, pad;
} sc_model;

enum { SC_IK_STATIC, SC_IK_TRACK, SC_IK_SPIN, SC_IK_ORBIT };
enum { SC_IF_FACE = 1, SC_IF_NOCULL = 2 };
typedef struct {                        /* INST: a model placed and moved */
    uint16_t model;
    uint8_t kind, flags;                /* SC_IK_*, SC_IF_* */
    uint16_t parent, track;             /* SC_NONE: none */
    uint32_t f0, f1;                    /* there for f0 <= f < f1 (f1 0: to the end) */
    float p[12];                        /* motion (tools/dbs.py) */
    float anim[4];                      /* first frame, frames, frames per second, phase (s) */
} sc_inst;

enum { SC_PF_ADD = 1, SC_PF_FLAT = 2, SC_PF_ALPHATEST = 4, SC_PF_NOFOG = 8 };
typedef struct {                        /* PART: a kind of particle */
    uint16_t tex, flags;                /* SC_PF_* */
    float life, life_jitter;            /* seconds; jitter as a fraction */
    float size0, size1;                 /* metres, at birth and at death */
    float speed, speed_jitter;
    float spread;                       /* cone half-angle (radians) around the direction */
    float gravity, drag, rise;          /* m/s^2 down, 1/s, m/s up */
    uint32_t rgba0, rgba1;              /* 0xRRGGBBAA at birth and at death */
    float spin, fade_in;                /* degrees/s at most; fraction of life */
    uint32_t pad;
} sc_part;

enum { SC_EF_MOVE = 1 };
typedef struct {                        /* EMIT: particles at a steady rate */
    uint16_t part, inst;                /* inst SC_NONE: pos and dir are in the world */
    uint32_t f0, f1;
    float pos[3], dir[3];               /* in the instance's space */
    float rate;                         /* per second */
    uint32_t seed, flags;               /* SC_EF_MOVE: particles move with the instance */
} sc_emit;

typedef struct {                        /* FXEV: a burst at one frame */
    uint32_t frame;
    uint16_t part, inst;
    float pos[3];
    uint32_t count, seed;
    float scale;                        /* size and speed */
} sc_fxev;

enum { SC_CK_PATH, SC_CK_CHASE, SC_CK_FIXED, SC_CK_MOUNT, SC_CK_ORBIT };
typedef struct {                        /* CAMS: a shot */
    uint32_t f0, f1;
    uint16_t kind, target, track, look_track;
    float fov;                          /* degrees; 0: GHDR's */
    float p[11];
} sc_cam;

enum { SC_SK_SCROLL, SC_SK_WARP, SC_SK_RAMP, SC_SK_PULSE };
typedef struct {                        /* SURF: a batch's texture or colour moves */
    uint16_t batch, kind;
    float p[7];
} sc_surf;

typedef struct {                        /* TRAK, parsed */
    uint32_t nkeys, flags;              /* 1: closed loop */
    float length;
    const float *keys;                  /* nkeys x (x, y, z, roll degrees), evenly spaced */
} sc_track;

typedef struct {                        /* VANM, parsed */
    uint32_t nverts, nframes, vfirst;
    float scale[3], origin[3];
    const uint8_t *data;                /* nframes x nverts x (x, y, z, normal) */
} sc_anim;

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
    /* Version 2 (all zero in version 1 files). */
    int version;
    const sc_ghdr *ghdr;
    int nmodel, ninst, npart, nemit, nfxev, ncam, nsurf, ntrack, nanim, nnormal;
    const sc_model *model;
    const sc_inst *inst;
    const sc_part *part;
    const sc_emit *emit;
    const sc_fxev *fxev;
    const sc_cam *cam;
    const sc_surf *surf;
    sc_track *track;
    sc_anim *anim;
    const float *normal;                /* nnormal x 3 */
    const uint8_t *vnormal;             /* per vertex: an index into normal */
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
