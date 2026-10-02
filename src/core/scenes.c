/* scenes.c - the game scenes (group scene in src/core/tests.json): a
 * version 2 .DBS drawn by the scene runtime (gs.c), every frame of its
 * story once, at the story's own frame numbers. */
#include "bench.h"
#include "gs.h"
#include "scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    scene sc;
    gs_world w;
    lv_world lv;
    int has_lv;
} game;

/* The file's CRC-32, for the T line (which content was measured). */
static unsigned long crc32(const uint8_t *p, long n)
{
    unsigned long c = 0xFFFFFFFFul;
    int k;
    while (n-- > 0) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320ul & (0ul - (c & 1)));
    }
    return c ^ 0xFFFFFFFFul;
}

static int scene_setup(tctx *t)
{
    game *G = (game *)calloc(1, sizeof *G);
    char err[96];
    const char *why = "";
    t->p = G;
    if (!G)
        return -1;
    if (sc_load(&G->sc, db_path(t->def->file), err, sizeof err) < 0) {
        char *c;
        snprintf(t->note, sizeof t->note, "why=%.55s", err);
        for (c = t->note; *c; c++)
            if (*c == ' ')
                *c = '_';
        return 1;                       /* no data: skipped, not failed */
    }
    if (gs_init(&G->w, &G->sc, &why) < 0) {
        snprintf(t->note, sizeof t->note, "why=%s", why);
        return -1;
    }
    if (G->sc.vis) {                    /* a Quake level: its world, two-pass lightmaps */
        if (lv_init(&G->lv, &G->sc, 0, (G->sc.ghdr->flags & SC_GF_QSKY) != 0, &why) < 0) {
            snprintf(t->note, sizeof t->note, "why=%s", why);
            return -1;
        }
        G->lv.surf = G->w.surf;
        G->w.lv = &G->lv;
        G->has_lv = 1;
    }
    if (sc_upload(&G->sc, !G->has_lv) < 0) {
        strcpy(t->note, "why=upload-failed");
        return -1;
    }
    if (G->has_lv) {                    /* static meshes for the models only: the world is gathered */
        int m, k;
        for (m = 0; m < G->sc.nmodel; m++)
            for (k = G->sc.model[m].batch0; k < G->sc.model[m].batch0 + G->sc.model[m].nbatch; k++) {
                const sc_batch *b = &G->sc.batch[k];
                if (!G->sc.mesh[k] && !G->w.cpu[k] &&
                    !(G->sc.mesh[k] = rb_mesh_create(G->sc.vert + b->vfirst, (int)b->vcount, G->sc.idx + b->ifirst,
                                                     (int)b->icount))) {
                    strcpy(t->note, "why=upload-failed");
                    return -1;
                }
            }
    }
    gs_prefetch(&G->w);
    t->frames = (int)G->sc.ghdr->frames;
    t->capture_frame = (int)G->sc.ghdr->capture;
    snprintf(t->note, sizeof t->note, "dbs=%08lx instances=%d particles=%d", crc32(G->sc.file, G->sc.size),
             G->sc.ninst, G->w.pmax);
    return 0;
}

static void scene_frame(tctx *t, int f)
{
    game *G = (game *)t->p;
    t->tris_drawn += gs_frame(&G->w, f);
}

static void scene_done(tctx *t)
{
    game *G = (game *)t->p;
    if (G) {
        if (G->has_lv)
            lv_free(&G->lv);
        gs_free(&G->w);
        sc_free(&G->sc);
        free(G);
    }
    t->p = NULL;
}

const test_impl impl_scene = { scene_setup, scene_frame, scene_done };
