/* level.c - the LibreQuake fly-through (tools/bsp.py makes the scene).
 *
 * Each frame does what Quake's renderer did (bspvis.c): find the camera's
 * leaf in the BSP, decompress its row of the potentially visible set, keep
 * the visible leaves inside the view frustum, mark their faces, and gather
 * the marked faces' triangles per (texture, lightmap) batch into index
 * lists. Then it draws: sky; the world with its textures; the lightmaps,
 * either as a second pass multiplied into the frame (DST_COLOR, ZERO:
 * GLQuake on one TMU) or already baked into the vertex colours; fences with
 * alpha testing; flame sprites as camera-facing alpha-tested quads built
 * every frame; and water, alpha-blended. Geometry varies per frame, so it
 * goes through rb_draw (vertex arrays or immediate mode under OpenGL;
 * display lists do not fit). */
#include "bench.h"
#include "bspvis.h"
#include "cull.h"
#include "scene.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    scene sc;
    int baked;
    bsp_vis bv;
    int *lm_order;                      /* batches in lightmap order, for the second pass */
    int sprite_batch;
    uint32_t *sprite_leaf;
    rb_vertex *sv;                      /* sprite quads */
    uint16_t *si;
    cull_frustum fr;
} level;

static const sc_batch *lm_sort_base;
static int cmp_lm(const void *a, const void *b)
{
    const sc_batch *x = &lm_sort_base[*(const int *)a], *y = &lm_sort_base[*(const int *)b];
    return (int)x->lm - (int)y->lm;
}

static int level_setup(tctx *t)
{
    level *L = (level *)calloc(1, sizeof *L);
    char err[96];
    const char *why = "";
    uint32_t i;
    t->p = L;
    if (!L)
        return -1;
    L->baked = t->def->param;
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
    if (bsp_init(&L->bv, &L->sc, &why) < 0) {
        snprintf(t->note, sizeof t->note, "why=%s", why);
        return -1;
    }
    if (sc_upload(&L->sc, 0) < 0) {
        strcpy(t->note, "why=upload-failed");
        return -1;
    }
    L->lm_order = (int *)calloc((size_t)L->sc.nbatch + 1, sizeof *L->lm_order);
    if (!L->lm_order)
        return -1;
    for (i = 0; i < (uint32_t)L->sc.nbatch; i++)
        L->lm_order[i] = (int)i;
    lm_sort_base = L->sc.batch;
    qsort(L->lm_order, (size_t)L->sc.nbatch, sizeof *L->lm_order, cmp_lm);
    L->sprite_batch = -1;
    for (i = 0; i < (uint32_t)L->sc.nbatch; i++)
        if (L->sc.batch[i].flags & SC_BF_SPRITE) {
            const sc_batch *b = &L->sc.batch[i];
            uint32_t k;
            L->sprite_batch = (int)i;
            L->sprite_leaf = (uint32_t *)malloc(b->vcount * sizeof *L->sprite_leaf);
            L->sv = (rb_vertex *)malloc(b->vcount * 4 * sizeof *L->sv);
            L->si = (uint16_t *)malloc(b->vcount * 6 * sizeof *L->si);
            if (!L->sprite_leaf || !L->sv || !L->si || b->vcount * 4 > 65535)
                return -1;
            for (k = 0; k < b->vcount; k++) {
                const rb_vertex *v = &L->sc.vert[b->vfirst + k];
                float p[3];
                p[0] = v->x; p[1] = v->y; p[2] = v->z;
                L->sprite_leaf[k] = (uint32_t)bsp_point_leaf(&L->bv, p);
            }
        }
    t->frames = (int)L->sc.view.frames;
    t->capture_frame = t->frames / 2;
    snprintf(t->note, sizeof t->note, "faces=%u leaves=%u path_frames=%d", (unsigned)L->bv.nfaces,
             (unsigned)L->bv.nleaves, t->frames);
    return 0;
}

static void draw_batch(level *L, int b)
{
    const sc_batch *bt = &L->sc.batch[b];
    if (L->bv.count[b])
        rb_draw(L->sc.vert + bt->vfirst, (int)bt->vcount, L->bv.scratch + bt->ifirst, (int)L->bv.count[b]);
}

static double sprites(level *L, const mat4 *mv)
{
    const sc_batch *b = &L->sc.batch[L->sprite_batch];
    float r[3], u[3];
    uint32_t k, n = 0;
    cull_view_axes(mv, r, u);
    for (k = 0; k < b->vcount; k++) {
        const rb_vertex *c = &L->sc.vert[b->vfirst + k];
        uint32_t lf = L->sprite_leaf[k];
        float w = c->u * 0.5f, h = c->v * 0.5f, lo[3], hi[3];
        rb_vertex *q = &L->sv[n * 4];
        int j;
        if (!bsp_leaf_visible(&L->bv, lf))
            continue;
        lo[0] = c->x - h; lo[1] = c->y - h; lo[2] = c->z - h;
        hi[0] = c->x + h; hi[1] = c->y + h; hi[2] = c->z + h;
        if (!cull_box(&L->fr, lo, hi))
            continue;
        for (j = 0; j < 4; j++) {
            float sx = (j == 1 || j == 2) ? w : -w, sy = j >= 2 ? h : -h;
            q[j] = *c;
            q[j].x = c->x + r[0] * sx + u[0] * sy;
            q[j].y = c->y + r[1] * sx + u[1] * sy;
            q[j].z = c->z + r[2] * sx + u[2] * sy;
            q[j].u = (j == 1 || j == 2) ? 1.0f : 0.0f;
            q[j].v = j >= 2 ? 0.0f : 1.0f;
        }
        L->si[n * 6 + 0] = (uint16_t)(n * 4); L->si[n * 6 + 1] = (uint16_t)(n * 4 + 1);
        L->si[n * 6 + 2] = (uint16_t)(n * 4 + 2); L->si[n * 6 + 3] = (uint16_t)(n * 4);
        L->si[n * 6 + 4] = (uint16_t)(n * 4 + 2); L->si[n * 6 + 5] = (uint16_t)(n * 4 + 3);
        n++;
    }
    if (n)
        rb_draw(L->sv, (int)n * 4, L->si, (int)n * 6);
    return 2.0 * n;
}

static void level_frame(tctx *t, int f)
{
    level *L = (level *)t->p;
    mat4 p, mv, clip;
    float eye[3], at[3], up[3] = { 0, 1, 0 };
    rb_state st;
    int i;
    double tris;
    rb_clear(0x000000);
    db_projection(&p, DB_FOVY, L->sc.view.znear, L->sc.view.zfar);
    sc_camera(&L->sc, f, eye, at);
    m4_lookat(&mv, eye, at, up);
    rb_set_matrices(&p, &mv);
    m4_mul(&clip, &p, &mv);
    cull_planes(&L->fr, &clip);
    tris = bsp_gather(&L->bv, &L->sc, eye, &L->fr);
    db_state_default(&st);
    st.bilinear = 1;
    st.cull = 1;
    /* Sky and the world's textures. */
    for (i = 0; i < L->sc.nbatch; i++) {
        const sc_batch *b = &L->sc.batch[i];
        if (b->flags & (SC_BF_TRANS | SC_BF_SPRITE))
            continue;
        st.tex = (b->flags & SC_BF_SKY) || !L->baked ? RB_TEX_REPLACE : RB_TEX_MODULATE;
        if (b->flags & SC_BF_ALPHATEST)
            st.tex = RB_TEX_MODULATE;           /* fences: vertex light in both modes */
        st.alpha_test = (b->flags & SC_BF_ALPHATEST) != 0;
        rb_set_state(&st);
        rb_tex_bind(L->sc.rtex[b->tex]);
        draw_batch(L, i);
    }
    st.alpha_test = 0;
    /* Lightmaps multiplied in, grouped by atlas page. */
    if (!L->baked) {
        st.blend = RB_BLEND_MUL;
        st.depth_write = 0;
        st.tex = RB_TEX_REPLACE;
        st.uvset = 1;
        rb_set_state(&st);
        for (i = 0; i < L->sc.nbatch; i++) {
            const sc_batch *b = &L->sc.batch[L->lm_order[i]];
            if (b->lm == SC_NONE || (b->flags & (SC_BF_SKY | SC_BF_TRANS | SC_BF_SPRITE | SC_BF_ALPHATEST)))
                continue;
            rb_tex_bind(L->sc.rtex[b->lm]);
            draw_batch(L, L->lm_order[i]);
            tris += L->bv.count[L->lm_order[i]] / 3;
        }
        st.blend = RB_BLEND_NONE;
        st.depth_write = 1;
        st.uvset = 0;
    }
    /* Flames. */
    if (L->sprite_batch >= 0) {
        st.tex = RB_TEX_MODULATE;
        st.alpha_test = 1;
        st.cull = 0;
        rb_set_state(&st);
        rb_tex_bind(L->sc.rtex[L->sc.batch[L->sprite_batch].tex]);
        tris += sprites(L, &mv);
        st.alpha_test = 0;
    }
    /* Water. */
    st.blend = RB_BLEND_ALPHA;
    st.depth_write = 0;
    st.tex = RB_TEX_MODULATE;
    st.cull = 0;
    rb_set_state(&st);
    for (i = 0; i < L->sc.nbatch; i++)
        if (L->sc.batch[i].flags & SC_BF_TRANS) {
            rb_tex_bind(L->sc.rtex[L->sc.batch[i].tex]);
            draw_batch(L, i);
        }
    rb_tex_bind(NULL);
    t->tris_drawn += tris;
}

static void level_done(tctx *t)
{
    level *L = (level *)t->p;
    if (!L)
        return;
    sc_free(&L->sc);
    bsp_free(&L->bv);
    free(L->lm_order); free(L->sprite_leaf); free(L->sv); free(L->si);
    free(L);
    t->p = NULL;
}

/* The registry's level tests (src/core/tests.json): param 0 two-pass lightmaps, 1 baked. */
const test_impl impl_level = { level_setup, level_frame, level_done };
