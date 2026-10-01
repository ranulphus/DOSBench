/* level.c - the LibreQuake fly-through (tools/bsp.py makes the scene).
 *
 * Each frame does what Quake's renderer did: find the camera's leaf in the
 * BSP, decompress its row of the potentially visible set, keep the visible
 * leaves inside the view frustum, mark their faces, and gather the marked
 * faces' triangles per (texture, lightmap) batch into index lists. Then it
 * draws: sky; the world with its textures; the lightmaps, either as a second
 * pass multiplied into the frame (DST_COLOR, ZERO: GLQuake on one TMU) or
 * already baked into the vertex colours; fences with alpha testing; flame
 * sprites as camera-facing alpha-tested quads built every frame; and water,
 * alpha-blended. Geometry varies per frame, so it goes through rb_draw
 * (vertex arrays or immediate mode under OpenGL; display lists do not fit). */
#include "bench.h"
#include "scene.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float n[3], d; int32_t child[2]; } lv_node;
typedef struct { int32_t contents, visofs; float mins[3], maxs[3]; uint32_t firstmark, nmarks; } lv_leaf;
typedef struct { uint16_t batch, icount; uint32_t ifirst; } lv_face;

typedef struct {
    scene sc;
    int baked;
    const lv_node *nodes;
    const lv_leaf *leaves;
    const uint32_t *marks;
    const lv_face *faces;
    const uint8_t *visdata;
    uint32_t nnodes, nleaves, nmarks, nfaces, visbytes, visleafs, headnode;
    uint32_t *facevis, visframe;
    uint8_t *pvs;
    long pvs_leaf;
    uint32_t *face0, *nface;            /* per batch: its faces */
    uint16_t *scratch;                  /* per-frame indices, at each batch's ifirst */
    uint32_t *count;                    /* per batch: indices gathered this frame */
    int *lm_order;                      /* batches in lightmap order, for the second pass */
    int sprite_batch;
    uint32_t *sprite_leaf;
    rb_vertex *sv;                      /* sprite quads */
    uint16_t *si;
    float planes[6][4];
} level;

static int parse_vis(level *L)
{
    const uint8_t *p = L->sc.vis;
    long need;
    uint32_t h[8];
    if (!p || L->sc.vis_size < 32)
        return -1;
    memcpy(h, p, sizeof h);
    L->nnodes = h[0]; L->nleaves = h[1]; L->nmarks = h[2]; L->nfaces = h[3];
    L->visbytes = h[4]; L->visleafs = h[5]; L->headnode = h[6];
    need = 32 + (long)L->nnodes * 24 + (long)L->nleaves * 40 + (long)L->nmarks * 4 + (long)L->nfaces * 8 + L->visbytes;
    if (need > L->sc.vis_size)
        return -1;
    L->nodes = (const lv_node *)(p + 32);
    L->leaves = (const lv_leaf *)(p + 32 + L->nnodes * 24);
    L->marks = (const uint32_t *)((const uint8_t *)L->leaves + L->nleaves * 40);
    L->faces = (const lv_face *)((const uint8_t *)L->marks + L->nmarks * 4);
    L->visdata = (const uint8_t *)L->faces + L->nfaces * 8;
    return 0;
}

static long point_leaf(const level *L, const float p[3])
{
    int32_t n = (int32_t)L->headnode;
    int guard = 0;
    while (n >= 0 && (uint32_t)n < L->nnodes && guard++ < 4096) {
        const lv_node *nd = &L->nodes[n];
        n = nd->child[nd->n[0] * p[0] + nd->n[1] * p[1] + nd->n[2] * p[2] - nd->d >= 0 ? 0 : 1];
    }
    n = -1 - n;
    return n >= 0 && (uint32_t)n < L->nleaves ? n : 0;
}

/* Quake's run-length coding: a zero byte is followed by a count of zero bytes. */
static void decompress_pvs(level *L, long leaf)
{
    uint32_t row = (L->visleafs + 7) / 8, o = 0;
    int32_t ofs = L->leaves[leaf].visofs;
    const uint8_t *in = L->visdata + (ofs > 0 ? ofs : 0), *end = L->visdata + L->visbytes;
    if (leaf == 0 || ofs < 0 || (uint32_t)ofs >= L->visbytes) {
        memset(L->pvs, 0xFF, row);           /* outside the map, or no data: everything */
        return;
    }
    while (o < row && in < end) {
        if (*in) {
            L->pvs[o++] = *in++;
            continue;
        }
        if (in + 1 >= end)
            break;
        {
            uint32_t c = in[1];
            in += 2;
            while (c-- && o < row)
                L->pvs[o++] = 0;
        }
    }
    while (o < row)
        L->pvs[o++] = 0;
}

static void frustum(level *L, const mat4 *clip)
{
    const float *m = clip->m;
    int i, k;
    for (i = 0; i < 3; i++)
        for (k = 0; k < 4; k++) {
            L->planes[i * 2][k] = m[k * 4 + 3] + m[k * 4 + i];
            L->planes[i * 2 + 1][k] = m[k * 4 + 3] - m[k * 4 + i];
        }
}

static int box_visible(const level *L, const float mins[3], const float maxs[3])
{
    int i;
    for (i = 0; i < 6; i++) {
        const float *p = L->planes[i];
        float x = p[0] >= 0 ? maxs[0] : mins[0], y = p[1] >= 0 ? maxs[1] : mins[1], z = p[2] >= 0 ? maxs[2] : mins[2];
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0)
            return 0;
    }
    return 1;
}

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
    if (parse_vis(L) < 0 || L->sc.view.kind != 1) {
        strcpy(t->note, "why=no-visibility-data");
        return -1;
    }
    if (sc_upload(&L->sc, 0) < 0) {
        strcpy(t->note, "why=upload-failed");
        return -1;
    }
    L->facevis = (uint32_t *)calloc(L->nfaces + 1, sizeof *L->facevis);
    L->pvs = (uint8_t *)malloc((L->visleafs + 7) / 8 + 1);
    L->face0 = (uint32_t *)calloc((size_t)L->sc.nbatch + 1, sizeof *L->face0);
    L->nface = (uint32_t *)calloc((size_t)L->sc.nbatch + 1, sizeof *L->nface);
    L->count = (uint32_t *)calloc((size_t)L->sc.nbatch + 1, sizeof *L->count);
    L->lm_order = (int *)calloc((size_t)L->sc.nbatch + 1, sizeof *L->lm_order);
    L->scratch = (uint16_t *)malloc((size_t)(L->sc.nidx + 1) * sizeof *L->scratch);
    if (!L->facevis || !L->pvs || !L->face0 || !L->nface || !L->count || !L->lm_order || !L->scratch)
        return -1;
    /* Faces are sorted by batch: find each batch's range. */
    for (i = 0; i < L->nfaces; i++) {
        uint16_t b = L->faces[i].batch;
        if (b >= L->sc.nbatch) {
            strcpy(t->note, "why=bad-face-batch");
            return -1;
        }
        if (!L->nface[b])
            L->face0[b] = i;
        L->nface[b]++;
    }
    for (i = 0; i < L->nmarks; i++)
        if (L->marks[i] >= L->nfaces) {
            strcpy(t->note, "why=bad-mark");
            return -1;
        }
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
                L->sprite_leaf[k] = (uint32_t)point_leaf(L, p);
            }
        }
    L->pvs_leaf = -1;
    t->frames = (int)L->sc.view.frames;
    t->capture_frame = t->frames / 2;
    snprintf(t->note, sizeof t->note, "faces=%u leaves=%u path_frames=%d", (unsigned)L->nfaces,
             (unsigned)L->nleaves, t->frames);
    return 0;
}

/* Mark and gather the visible faces; returns the triangles gathered. */
static double gather(level *L, const float eye[3])
{
    long leaf = point_leaf(L, eye);
    uint32_t l, b, k;
    double tris = 0;
    if (leaf != L->pvs_leaf) {
        decompress_pvs(L, leaf);
        L->pvs_leaf = leaf;
    }
    if (++L->visframe == 0)
        L->visframe = 1;
    for (l = 1; l < L->nleaves && l <= L->visleafs; l++) {
        const lv_leaf *lf = &L->leaves[l];
        if (!(L->pvs[(l - 1) >> 3] & (1u << ((l - 1) & 7))) || !box_visible(L, lf->mins, lf->maxs))
            continue;
        for (k = 0; k < lf->nmarks && lf->firstmark + k < L->nmarks; k++)
            L->facevis[L->marks[lf->firstmark + k]] = L->visframe;
    }
    for (b = 0; b < (uint32_t)L->sc.nbatch; b++) {
        const sc_batch *bt = &L->sc.batch[b];
        uint16_t *out = L->scratch + bt->ifirst;
        uint32_t n = 0, f;
        for (f = L->face0[b]; f < L->face0[b] + L->nface[b]; f++)
            if (L->facevis[f] == L->visframe) {
                const lv_face *fc = &L->faces[f];
                memcpy(out + n, L->sc.idx + fc->ifirst, (size_t)fc->icount * sizeof *out);
                n += fc->icount;
            }
        L->count[b] = n;
        tris += n / 3;
    }
    return tris;
}

static void draw_batch(level *L, int b)
{
    const sc_batch *bt = &L->sc.batch[b];
    if (L->count[b])
        rb_draw(L->sc.vert + bt->vfirst, (int)bt->vcount, L->scratch + bt->ifirst, (int)L->count[b]);
}

static double sprites(level *L, const mat4 *mv)
{
    const sc_batch *b = &L->sc.batch[L->sprite_batch];
    /* Camera right and up in world space: the first two rows of the view matrix. */
    float rx = mv->m[0], ry = mv->m[4], rz = mv->m[8], ux = mv->m[1], uy = mv->m[5], uz = mv->m[9];
    uint32_t k, n = 0;
    for (k = 0; k < b->vcount; k++) {
        const rb_vertex *c = &L->sc.vert[b->vfirst + k];
        uint32_t lf = L->sprite_leaf[k];
        float w = c->u * 0.5f, h = c->v * 0.5f, lo[3], hi[3];
        rb_vertex *q = &L->sv[n * 4];
        int j;
        if (lf == 0 || lf > L->visleafs || !(L->pvs[(lf - 1) >> 3] & (1u << ((lf - 1) & 7))))
            continue;
        lo[0] = c->x - h; lo[1] = c->y - h; lo[2] = c->z - h;
        hi[0] = c->x + h; hi[1] = c->y + h; hi[2] = c->z + h;
        if (!box_visible(L, lo, hi))
            continue;
        for (j = 0; j < 4; j++) {
            float sx = (j == 1 || j == 2) ? w : -w, sy = j >= 2 ? h : -h;
            q[j] = *c;
            q[j].x = c->x + rx * sx + ux * sy;
            q[j].y = c->y + ry * sx + uy * sy;
            q[j].z = c->z + rz * sx + uz * sy;
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
    frustum(L, &clip);
    tris = gather(L, eye);
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
            tris += L->count[L->lm_order[i]] / 3;
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
    free(L->facevis); free(L->pvs); free(L->face0); free(L->nface); free(L->count);
    free(L->lm_order); free(L->scratch); free(L->sprite_leaf); free(L->sv); free(L->si);
    free(L);
    t->p = NULL;
}

/* The registry's level tests (src/core/tests.json): param 0 two-pass lightmaps, 1 baked. */
const test_impl impl_level = { level_setup, level_frame, level_done };
