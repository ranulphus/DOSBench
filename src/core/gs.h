/* gs.h - the game-scene runtime (gs.c, gsfx.c): draws a version 2 .DBS
 * scene's frame f as a pure function of f.
 *
 * Every frame: the shot (CAMS) gives the camera; each instance (INST) is
 * placed by its motion at its own time, under its parent; spheres outside
 * the frustum are dropped and distant models swap to their LOD; lit and
 * animated models get their vertex colours (and positions) on the CPU, as
 * Quake did for its models; surfaces scroll, warp, ramp and pulse. Then
 * the passes: sky, opaque and alpha-tested, decals, blended (far first),
 * blended particles (far first), additive, additive particles. Particles
 * keep no state: each is a hash of its emitter and number, born at a known
 * time from where its emitter was then. */
#ifndef GS_H
#define GS_H
#include "bench.h"
#include "cull.h"
#include "scene.h"

typedef struct {
    float pos[3], size, angle, depth;
    uint32_t rgba;
    uint16_t part, flat;
} gs_particle;

typedef struct {
    scene *sc;
    const sc_ghdr *g;
    /* This frame. */
    double t;                           /* story time, seconds */
    float eye[3], at[3], up[3], fov;
    mat4 proj, view;
    cull_frustum fr;
    mat4 *world;                        /* per instance */
    float *radius;                      /* per instance: world bounding radius (0: culled or gone) */
    uint16_t *drawn;                    /* per instance: the model drawn (LOD), SC_NONE if none */
    int *order;                         /* instances, sorted for the blended pass */
    float *depth;
    /* Scratch. */
    uint8_t *cpu;                       /* per batch: drawn from a CPU copy (lit, animated, surface) */
    int16_t *surf;                      /* per batch: its SURF record, or -1 */
    rb_vertex *vbuf;                    /* the CPU copy */
    long vbuf_n;
    gs_particle *pt;
    int pmax, np;
    rb_vertex *pv;                      /* particle quads */
    uint16_t *pi;
    int *porder;
    rb_state last;                      /* the state last set: unchanged states are not sent again */
    int have_last;
    rb_tex *last_tex;
    double tris;                        /* triangles drawn this frame */
} gs_world;

/* After sc_upload(sc, 1). 0, or -1 with why. */
int  gs_init(gs_world *w, scene *sc, const char **why);
void gs_free(gs_world *w);
/* Draw every texture once (the Glide backend downloads at first use), so
 * no timed frame downloads one. */
void gs_prefetch(gs_world *w);
/* Draw frame f; returns the triangles drawn. */
double gs_frame(gs_world *w, int f);

/* Pieces, for gs_frame, gsfx.c and the tests. */
void gs_track_at(const sc_track *tr, double d, float pos[3], float fwd[3], float up[3]);
/* Instance i's placement at story time t; 0 when it is not there then. */
int  gs_inst_at(const scene *sc, int i, double t, mat4 *m);
/* The camera at frame f. */
void gs_camera(const scene *sc, int f, float eye[3], float at[3], float up[3], float *fov);
/* Stateless random numbers in [0, 1). */
float gs_rand(uint32_t seed, uint32_t n, uint32_t channel);

/* gsfx.c */
int  gsfx_capacity(const scene *sc);                    /* most particles alive at once */
void gsfx_particles(gs_world *w);                       /* fill w->pt for w->t */
void gsfx_draw_particles(gs_world *w, int additive);
/* A batch's vertices for this frame into w->vbuf (lit with lut when not NULL). */
void gsfx_vertices(gs_world *w, int inst, int batch, const uint16_t *lut);
void gs_set_state(gs_world *w, const rb_state *st);
void gs_bind(gs_world *w, rb_tex *t);

#endif
