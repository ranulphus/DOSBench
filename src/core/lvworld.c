/* lvworld.c - a Quake level's world (lvworld.h).
 *
 * Each frame does what Quake's renderer did (bspvis.c): find the camera's
 * leaf, decompress its row of the potentially visible set, keep the visible
 * leaves inside the view frustum, mark their faces, and gather the marked
 * faces' triangles per (texture, lightmap) batch. Then: sky; the world with
 * its textures; the lightmaps, either as a second pass multiplied into the
 * frame (DST_COLOR, ZERO: GLQuake on one TMU) or already baked into the
 * vertex colours; fences with alpha testing; flame sprites as camera-facing
 * alpha-tested quads built every frame; and, last, water, alpha-blended.
 * Geometry varies per frame, so it goes through rb_draw (vertex arrays or
 * immediate mode under OpenGL; display lists do not fit). The two-layer sky
 * (qsky) scrolls a solid back layer and a faster cut-out front layer across
 * a flattened dome around the eye, as Quake did. */
#include "lvworld.h"
#include "bench.h"
#include "gs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const sc_batch *lm_sort_base;
static int cmp_lm(const void *a, const void *b)
{
    const sc_batch *x = &lm_sort_base[*(const int *)a], *y = &lm_sort_base[*(const int *)b];
    return (int)x->lm - (int)y->lm;
}

int lv_init(lv_world *L, scene *sc, int baked, int qsky, const char **why)
{
    uint32_t i;
    long maxv = 1;
    memset(L, 0, sizeof *L);
    L->sc = sc;
    L->baked = baked;
    L->qsky = qsky;
    if (bsp_init(&L->bv, sc, why) < 0)
        return -1;
    L->lm_order = (int *)calloc((size_t)sc->nbatch + 1, sizeof *L->lm_order);
    if (!L->lm_order) {
        *why = "out-of-memory";
        return -1;
    }
    for (i = 0; i < (uint32_t)sc->nbatch; i++)
        L->lm_order[i] = (int)i;
    lm_sort_base = sc->batch;
    qsort(L->lm_order, (size_t)sc->nbatch, sizeof *L->lm_order, cmp_lm);
    L->sprite_batch = -1;
    for (i = 0; i < (uint32_t)sc->nbatch; i++) {
        const sc_batch *b = &sc->batch[i];
        if (!(b->flags & SC_BF_SPRITE) && (long)b->vcount > maxv)
            maxv = (long)b->vcount;
        if (b->flags & SC_BF_SPRITE) {
            uint32_t k;
            L->sprite_batch = (int)i;
            L->sprite_leaf = (uint32_t *)malloc(b->vcount * sizeof *L->sprite_leaf);
            L->sv = (rb_vertex *)malloc(b->vcount * 4 * sizeof *L->sv);
            L->si = (uint16_t *)malloc(b->vcount * 6 * sizeof *L->si);
            if (!L->sprite_leaf || !L->sv || !L->si || b->vcount * 4 > 65535) {
                *why = "out-of-memory";
                return -1;
            }
            for (k = 0; k < b->vcount; k++) {
                const rb_vertex *v = &sc->vert[b->vfirst + k];
                float p[3];
                p[0] = v->x; p[1] = v->y; p[2] = v->z;
                L->sprite_leaf[k] = (uint32_t)bsp_point_leaf(&L->bv, p);
            }
        }
    }
    L->tmp_n = maxv;
    L->tmp = (rb_vertex *)malloc(sizeof *L->tmp * (size_t)maxv);
    if (!L->tmp) {
        *why = "out-of-memory";
        return -1;
    }
    return 0;
}

void lv_free(lv_world *L)
{
    bsp_free(&L->bv);
    free(L->lm_order); free(L->sprite_leaf); free(L->sv); free(L->si); free(L->tmp);
    memset(L, 0, sizeof *L);
}

static void draw_batch(lv_world *L, int b)
{
    const sc_batch *bt = &L->sc->batch[b];
    if (L->bv.count[b])
        rb_draw(L->sc->vert + bt->vfirst, (int)bt->vcount, L->bv.scratch + bt->ifirst, (int)L->bv.count[b]);
}

/* A batch from a per-frame copy of its vertices (the gathered indices still apply). */
static void draw_copy(lv_world *L, int b)
{
    const sc_batch *bt = &L->sc->batch[b];
    if (L->bv.count[b])
        rb_draw(L->tmp, (int)bt->vcount, L->bv.scratch + bt->ifirst, (int)L->bv.count[b]);
}

/* Quake's sky: texture coordinates from the direction to each vertex, the dome
 * flattened three times, scrolling at speed texels per second. */
static void sky_uvs(lv_world *L, int b, const float eye[3], double t, double speed)
{
    const sc_batch *bt = &L->sc->batch[b];
    double scroll = fmod(t * speed, 128.0);
    uint32_t k;
    memcpy(L->tmp, L->sc->vert + bt->vfirst, sizeof *L->tmp * bt->vcount);
    for (k = 0; k < bt->vcount; k++) {
        rb_vertex *v = &L->tmp[k];
        float dx = v->x - eye[0], dy = (v->y - eye[1]) * 3, dz = v->z - eye[2];
        float len = (float)sqrt(dx * dx + dy * dy + dz * dz), s = len > 0 ? 378.0f / len : 0;
        v->u = (float)((scroll + dx * s) / 128.0);
        v->v = (float)((scroll - dz * s) / 128.0);
    }
}

static double sprites(lv_world *L, const mat4 *mv, const cull_frustum *fr)
{
    const scene *sc = L->sc;
    const sc_batch *b = &sc->batch[L->sprite_batch];
    float r[3], u[3];
    uint32_t k, n = 0;
    cull_view_axes(mv, r, u);
    for (k = 0; k < b->vcount; k++) {
        const rb_vertex *c = &sc->vert[b->vfirst + k];
        uint32_t lf = L->sprite_leaf[k];
        float w = c->u * 0.5f, h = c->v * 0.5f, lo[3], hi[3];
        rb_vertex *q = &L->sv[n * 4];
        int j;
        if (!bsp_leaf_visible(&L->bv, lf))
            continue;
        lo[0] = c->x - h; lo[1] = c->y - h; lo[2] = c->z - h;
        hi[0] = c->x + h; hi[1] = c->y + h; hi[2] = c->z + h;
        if (!cull_box(fr, lo, hi))
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

double lv_draw_opaque(lv_world *L, const mat4 *view, const float eye[3], const cull_frustum *fr, double t)
{
    scene *sc = L->sc;
    rb_state st;
    int i;
    double tris = bsp_gather(&L->bv, sc, eye, fr);
    db_state_default(&st);
    st.bilinear = 1;
    st.cull = 1;
    /* Sky and the world's textures. */
    for (i = 0; i < sc->nbatch; i++) {
        const sc_batch *b = &sc->batch[i];
        if (b->flags & (SC_BF_TRANS | SC_BF_SPRITE))
            continue;
        if (L->qsky && (b->flags & SC_BF_SKY) && b->lm != SC_NONE) {
            if (!L->bv.count[i])
                continue;
            st.tex = RB_TEX_REPLACE;            /* the back layer, then the front over it */
            st.alpha_test = 0;
            rb_set_state(&st);
            rb_tex_bind(sc->rtex[b->tex]);
            sky_uvs(L, i, eye, t, 8.0);
            draw_copy(L, i);
            st.tex = RB_TEX_MODULATE;
            st.blend = RB_BLEND_ALPHA;
            st.depth_write = 0;
            rb_set_state(&st);
            rb_tex_bind(sc->rtex[b->lm]);
            sky_uvs(L, i, eye, t, 16.0);
            draw_copy(L, i);
            tris += L->bv.count[i] / 3;
            st.blend = RB_BLEND_NONE;
            st.depth_write = 1;
            continue;
        }
        st.tex = (b->flags & SC_BF_SKY) || !L->baked ? RB_TEX_REPLACE : RB_TEX_MODULATE;
        if (b->flags & SC_BF_ALPHATEST)
            st.tex = RB_TEX_MODULATE;           /* fences: vertex light in both modes */
        st.alpha_test = (b->flags & SC_BF_ALPHATEST) != 0;
        rb_set_state(&st);
        rb_tex_bind(sc->rtex[b->tex]);
        if (L->surf && L->surf[i] >= 0 && L->bv.count[i]) {     /* lava: its surface moves */
            memcpy(L->tmp, sc->vert + b->vfirst, sizeof *L->tmp * b->vcount);
            gsfx_surface(&sc->surf[L->surf[i]], L->tmp, b->vcount, t, (uint32_t)i);
            draw_copy(L, i);
        } else
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
        for (i = 0; i < sc->nbatch; i++) {
            const sc_batch *b = &sc->batch[L->lm_order[i]];
            if (b->lm == SC_NONE || (b->flags & (SC_BF_SKY | SC_BF_TRANS | SC_BF_SPRITE | SC_BF_ALPHATEST)))
                continue;
            rb_tex_bind(sc->rtex[b->lm]);
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
        rb_tex_bind(sc->rtex[sc->batch[L->sprite_batch].tex]);
        tris += sprites(L, view, fr);
    }
    return tris;
}

double lv_draw_water(lv_world *L, double t)
{
    scene *sc = L->sc;
    rb_state st;
    int i;
    db_state_default(&st);
    st.bilinear = 1;
    st.blend = RB_BLEND_ALPHA;
    st.depth_write = 0;
    st.tex = RB_TEX_MODULATE;
    rb_set_state(&st);
    for (i = 0; i < sc->nbatch; i++)
        if (sc->batch[i].flags & SC_BF_TRANS) {
            rb_tex_bind(sc->rtex[sc->batch[i].tex]);
            if (L->surf && L->surf[i] >= 0 && L->bv.count[i]) {
                const sc_batch *b = &sc->batch[i];
                memcpy(L->tmp, sc->vert + b->vfirst, sizeof *L->tmp * b->vcount);
                gsfx_surface(&sc->surf[L->surf[i]], L->tmp, b->vcount, t, (uint32_t)i);
                draw_copy(L, i);
            } else
                draw_batch(L, i);
        }
    return 0;
}
