/* lvworld.h - a Quake level's world, drawn as GLQuake did (lvworld.c): the
 * level fly-through (level.c) and the Arena scene (scenes.c) share it. */
#ifndef LVWORLD_H
#define LVWORLD_H
#include "bspvis.h"
#include "cull.h"
#include "scene.h"

typedef struct {
    scene *sc;                          /* not owned */
    int baked;                          /* lighting in the vertex colours, not a second pass */
    int qsky;                           /* two-layer Quake sky: sky batches carry the front layer as lm */
    bsp_vis bv;
    int *lm_order;                      /* batches in lightmap order, for the second pass */
    int sprite_batch;
    uint32_t *sprite_leaf;
    rb_vertex *sv;                      /* sprite quads */
    uint16_t *si;
    rb_vertex *tmp;                     /* per-frame copies (sky, surfaces) */
    long tmp_n;
    const int16_t *surf;                /* per batch: its SURF record or -1 (NULL: none) */
} lv_world;

/* After sc_load, before sc_upload. 0, or -1 with why. */
int  lv_init(lv_world *L, scene *sc, int baked, int qsky, const char **why);
void lv_free(lv_world *L);
/* Gather what eye sees through the frustum, then draw the sky, the textured
 * world, the lightmaps, fences and flames, under the matrices already set;
 * view is their modelview (for the flames' axes). t: story time (sky,
 * surfaces). Returns the triangles drawn. */
double lv_draw_opaque(lv_world *L, const mat4 *view, const float eye[3], const cull_frustum *fr, double t);
/* Then the water (blended), from the same gathering. */
double lv_draw_water(lv_world *L, double t);

#endif
