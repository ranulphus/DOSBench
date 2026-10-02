/* level.c - the LibreQuake fly-through (tools/bsp.py makes the scene): the
 * camera along the map's path, the world drawn as GLQuake did (lvworld.c),
 * with its lightmaps as a second pass (param 0) or baked into the vertex
 * colours (param 1). */
#include "bench.h"
#include "cull.h"
#include "lvworld.h"
#include "scene.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    scene sc;
    lv_world lv;
    cull_frustum fr;
} level;

static int level_setup(tctx *t)
{
    level *L = (level *)calloc(1, sizeof *L);
    char err[96];
    const char *why = "";
    t->p = L;
    if (!L)
        return -1;
    if (sc_load(&L->sc, db_path(t->def->file), err, sizeof err) < 0) {
        char *c;
        snprintf(t->note, sizeof t->note, "why=%.55s", err);
        for (c = t->note; *c; c++)
            if (*c == ' ')
                *c = '_';
        return 1;
    }
    if (L->sc.view.kind != 1) {
        strcpy(t->note, "why=no-visibility-data");
        return -1;
    }
    if (lv_init(&L->lv, &L->sc, t->def->param, 0, &why) < 0) {
        snprintf(t->note, sizeof t->note, "why=%s", why);
        return -1;
    }
    if (sc_upload(&L->sc, 0) < 0) {
        strcpy(t->note, "why=upload-failed");
        return -1;
    }
    t->frames = (int)L->sc.view.frames;
    t->capture_frame = t->frames / 2;
    snprintf(t->note, sizeof t->note, "faces=%u leaves=%u path_frames=%d", (unsigned)L->lv.bv.nfaces,
             (unsigned)L->lv.bv.nleaves, t->frames);
    return 0;
}

static void level_frame(tctx *t, int f)
{
    level *L = (level *)t->p;
    mat4 p, mv, clip;
    float eye[3], at[3], up[3] = { 0, 1, 0 };
    double tris;
    rb_clear(0x000000);
    db_projection(&p, DB_FOVY, L->sc.view.znear, L->sc.view.zfar);
    sc_camera(&L->sc, f, eye, at);
    m4_lookat(&mv, eye, at, up);
    rb_set_matrices(&p, &mv);
    m4_mul(&clip, &p, &mv);
    cull_planes(&L->fr, &clip);
    tris = lv_draw_opaque(&L->lv, &mv, eye, &L->fr, 0);
    tris += lv_draw_water(&L->lv, 0);
    rb_tex_bind(NULL);
    t->tris_drawn += tris;
}

static void level_done(tctx *t)
{
    level *L = (level *)t->p;
    if (!L)
        return;
    lv_free(&L->lv);
    sc_free(&L->sc);
    free(L);
    t->p = NULL;
}

/* The registry's level tests (src/core/tests.json): param 0 two-pass lightmaps, 1 baked. */
const test_impl impl_level = { level_setup, level_frame, level_done };
