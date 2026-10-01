/* synth.c - the basic check and the synthetic tests S1-S4 (docs/tests.md).
 *
 * Geometry is laid out in pixel units and drawn through the standard
 * perspective projection at a fixed eye distance (db_pixel_view), so every
 * test exercises the same transform path as the scenes. Depth testing is off
 * unless stated, so the image is the last thing drawn. */
#include "bench.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EYE_D 10.0f

typedef struct {
    rb_vertex *v;
    uint16_t *idx;
    int nv, ni;
    rb_mesh *mesh;
    rb_tex *tex[32];
    int ntex;
    int copies, layers, chunk, mode;
    uint8_t *img[2];
    int saved_submit;
} synth;

static synth *alloc_synth(tctx *t)
{
    synth *s = (synth *)calloc(1, sizeof *s);
    t->p = s;
    if (s)
        s->saved_submit = -1;
    return s;
}

static void free_synth(tctx *t)
{
    synth *s = (synth *)t->p;
    int i;
    if (!s)
        return;
    rb_mesh_free(s->mesh);
    for (i = 0; i < s->ntex; i++)
        rb_tex_free(s->tex[i]);
    free(s->v);
    free(s->idx);
    free(s->img[0]);
    free(s->img[1]);
    if (s->saved_submit >= 0)
        rb_set_submit(s->saved_submit);
    free(s);
    t->p = NULL;
}

static void vtx(rb_vertex *v, float x, float y, uint32_t rgba, float u, float w)
{
    v->x = x; v->y = y; v->z = 0;
    v->c[0] = (uint8_t)(rgba >> 24); v->c[1] = (uint8_t)(rgba >> 16);
    v->c[2] = (uint8_t)(rgba >> 8); v->c[3] = (uint8_t)rgba;
    v->u = u; v->v = w;
    v->u2 = u; v->v2 = w;
}

/* Quad as two counter-clockwise triangles (in pixel space, y down). */
static void quad_idx(uint16_t *ix, int base)
{
    ix[0] = (uint16_t)base; ix[1] = (uint16_t)(base + 2); ix[2] = (uint16_t)(base + 1);
    ix[3] = (uint16_t)base; ix[4] = (uint16_t)(base + 3); ix[5] = (uint16_t)(base + 2);
}

static void quad(rb_vertex *v, float x0, float y0, float x1, float y1, const uint32_t c[4], float tscale)
{
    vtx(&v[0], x0, y0, c[0], x0 / tscale, y0 / tscale);
    vtx(&v[1], x1, y0, c[1], x1 / tscale, y0 / tscale);
    vtx(&v[2], x1, y1, c[2], x1 / tscale, y1 / tscale);
    vtx(&v[3], x0, y1, c[3], x0 / tscale, y1 / tscale);
}

static void pixel_matrices(float dx, float dy)
{
    mat4 p, mv;
    db_projection(&p, DB_FOVY, DB_ZNEAR, DB_ZFAR);
    db_pixel_view(&mv, EYE_D);
    if (dx != 0 || dy != 0)
        m4_translate(&mv, dx, dy, 0);
    rb_set_matrices(&p, &mv);
}

static rb_tex *make_tex(synth *s, int size, int kind, int seed, int flags)
{
    uint8_t *img = (uint8_t *)malloc((size_t)size * size * 4);
    rb_tex *t;
    if (!img)
        return NULL;
    db_texture(img, size, size, kind, seed);
    t = rb_tex_create(size, size, img, flags);
    free(img);
    if (t)
        s->tex[s->ntex++] = t;
    return t;
}

/* ---- B0: clear, one Gouraud triangle, one textured quad ---------------- */
static int basic_setup(tctx *t)
{
    static const uint32_t white[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    synth *s = alloc_synth(t);
    float w = (float)db.w, h = (float)db.h;
    if (!s)
        return -1;
    s->v = (rb_vertex *)calloc(7, sizeof *s->v);
    s->idx = (uint16_t *)calloc(9, sizeof *s->idx);
    if (!s->v || !s->idx || !make_tex(s, 64, 0, 1, RB_TF_MIPMAP))
        return -1;
    vtx(&s->v[0], w * 0.10f, h * 0.85f, 0xFF2020FFu, 0, 0);
    vtx(&s->v[1], w * 0.45f, h * 0.85f, 0x20FF20FFu, 0, 0);
    vtx(&s->v[2], w * 0.25f, h * 0.15f, 0x2020FFFFu, 0, 0);
    s->idx[0] = 0; s->idx[1] = 1; s->idx[2] = 2;
    quad(&s->v[3], w * 0.55f, h * 0.2f, w * 0.9f, h * 0.2f + w * 0.35f, white, w * 0.35f);
    s->nv = 7;
    t->tris = 3;
    return 0;
}

static void basic_frame(tctx *t, int f)
{
    synth *s = (synth *)t->p;
    rb_state st;
    uint16_t qi[6];
    (void)f;
    rb_clear(0x203040);
    pixel_matrices(0, 0);
    db_state_default(&st);
    rb_set_state(&st);
    rb_draw(s->v, 3, s->idx, 3);
    st.tex = RB_TEX_MODULATE;
    st.bilinear = 1;
    rb_set_state(&st);
    rb_tex_bind(s->tex[0]);
    quad_idx(qi, 0);
    rb_draw(&s->v[3], 4, qi, 6);
    rb_tex_bind(NULL);
}

/* ---- S1: fill rate ------------------------------------------------------ */
enum { F_FLAT, F_GOURAUD, F_POINT, F_BILINEAR, F_BLEND, F_FOG };

static int fill_setup(tctx *t)
{
    synth *s = alloc_synth(t);
    int L, i, variant = t->def->param;
    float m = 2.0f;
    if (!s)
        return -1;
    L = db.quick ? 2 : 8;
    s->layers = L;
    s->v = (rb_vertex *)calloc((size_t)L * 4, sizeof *s->v);
    s->idx = (uint16_t *)calloc((size_t)L * 6, sizeof *s->idx);
    if (!s->v || !s->idx)
        return -1;
    for (i = 0; i < L; i++) {
        static const uint32_t corners[4] = { 0xFF404000u, 0x40FF4000u, 0x4040FF00u, 0xFFFF4000u };
        uint32_t flat = ((uint32_t)(64 + (i * 53) % 192) << 24) | ((uint32_t)(64 + (i * 97) % 192) << 16) | 0xA000u;
        uint32_t c[4];
        int k;
        for (k = 0; k < 4; k++) {
            c[k] = variant == F_FLAT ? flat : variant == F_GOURAUD ? corners[(k + i) & 3] : 0xFFFFFF00u;
            c[k] |= variant == F_BLEND && i > 0 ? 0x80u : 0xFFu;
        }
        quad(&s->v[i * 4], -m, -m, db.w + m, db.h + m, c, 256.0f);
        /* Offset each layer's texture so the layers differ. */
        for (k = 0; k < 4; k++) {
            s->v[i * 4 + k].u += i * 0.125f;
            s->v[i * 4 + k].v += i * 0.0625f;
        }
        quad_idx(&s->idx[i * 6], i * 4);
    }
    if (variant >= F_POINT && !make_tex(s, 256, 0, 3, 0))
        return -1;
    t->pixels = (double)L * db.w * db.h;
    t->tris = 2.0 * L;
    return 0;
}

static void fill_frame(tctx *t, int f)
{
    synth *s = (synth *)t->p;
    int variant = t->def->param;
    rb_state st;
    (void)f;
    pixel_matrices(0, 0);
    memset(&st, 0, sizeof st);
    st.tex = variant >= F_POINT ? RB_TEX_MODULATE : RB_TEX_OFF;
    st.bilinear = variant >= F_BILINEAR;
    st.fog = variant == F_FOG;
    st.fog_start = 0;
    st.fog_end = 2 * EYE_D;
    st.fog_rgb = 0x8090A0;
    rb_set_state(&st);
    if (st.tex)
        rb_tex_bind(s->tex[0]);
    if (variant == F_BLEND) {
        /* The first layer is opaque so the frame does not depend on the last. */
        rb_draw(s->v, 4, s->idx, 6);
        st.blend = RB_BLEND_ALPHA;
        rb_set_state(&st);
        rb_draw(s->v, s->layers * 4, s->idx + 6, (s->layers - 1) * 6);
    } else {
        rb_draw(s->v, s->layers * 4, s->idx, s->layers * 6);
    }
    rb_tex_bind(NULL);
}

/* ---- S2: triangle rate --------------------------------------------------
 * param: area in pixels (4, 16, 64) + 1000 * topology (0 list, 1 mesh)
 *        + 10000 if textured + 100000 * forced submit (1 arrays, 2 lists,
 *        3 immediate). A grid of cells, two triangles each; the list uses
 *        independent vertices, the mesh shares them. Copies of the grid,
 *        slightly offset, make up the per-frame count. */
static int tri_setup(tctx *t)
{
    synth *s = alloc_synth(t);
    int p = t->def->param, area = p % 1000, mesh = (p / 1000) % 10, textured = (p / 10000) % 10;
    int forced = p / 100000, target = db.quick ? 2048 : 16384;
    float leg = (float)sqrt(2.0 * area), cell = mesh ? leg : leg + 1.0f;
    int cols, rows, side, x, y, k;
    if (!s)
        return -1;
    side = (int)sqrt(target / 2.0);
    cols = side;
    if (cols * cell > db.w - 20) cols = (int)((db.w - 20) / cell);
    rows = target / 2 / cols;
    if (rows * cell > db.h - 20) rows = (int)((db.h - 20) / cell);
    s->copies = (int)(target / (2.0 * cols * rows) + 0.5);
    if (s->copies < 1) s->copies = 1;
    s->nv = mesh ? (cols + 1) * (rows + 1) : cols * rows * 6;
    s->ni = cols * rows * 6;
    if (s->nv > 65535)
        return -1;
    s->v = (rb_vertex *)calloc((size_t)s->nv, sizeof *s->v);
    s->idx = (uint16_t *)calloc((size_t)s->ni, sizeof *s->idx);
    if (!s->v || !s->idx)
        return -1;
    k = 0;
    if (mesh) {
        for (y = 0; y <= rows; y++)
            for (x = 0; x <= cols; x++) {
                uint32_t c = 0x000000FFu | ((uint32_t)(64 + x * 191 / cols) << 24) | ((uint32_t)(64 + y * 191 / rows) << 16) |
                             ((uint32_t)(((x ^ y) & 1) ? 220 : 90) << 8);
                vtx(&s->v[y * (cols + 1) + x], 10 + x * cell, 10 + y * cell, c, x * cell / 64.0f, y * cell / 64.0f);
            }
        for (y = 0; y < rows; y++)
            for (x = 0; x < cols; x++) {
                int i0 = y * (cols + 1) + x;
                s->idx[k++] = (uint16_t)i0; s->idx[k++] = (uint16_t)(i0 + cols + 1); s->idx[k++] = (uint16_t)(i0 + 1);
                s->idx[k++] = (uint16_t)(i0 + 1); s->idx[k++] = (uint16_t)(i0 + cols + 1); s->idx[k++] = (uint16_t)(i0 + cols + 2);
            }
    } else {
        for (y = 0; y < rows; y++)
            for (x = 0; x < cols; x++) {
                float x0 = 10 + x * cell, y0 = 10 + y * cell;
                uint32_t c = 0x000000FFu | ((uint32_t)(64 + x * 191 / cols) << 24) | ((uint32_t)(64 + y * 191 / rows) << 16) |
                             ((uint32_t)(((x ^ y) & 1) ? 220 : 90) << 8);
                rb_vertex *v = &s->v[k];
                vtx(&v[0], x0, y0, c, x0 / 64.0f, y0 / 64.0f);
                vtx(&v[1], x0, y0 + leg, c ^ 0x30300000u, x0 / 64.0f, (y0 + leg) / 64.0f);
                vtx(&v[2], x0 + leg, y0, c ^ 0x00303000u, (x0 + leg) / 64.0f, y0 / 64.0f);
                vtx(&v[3], x0 + leg, y0, c ^ 0x00303000u, (x0 + leg) / 64.0f, y0 / 64.0f);
                vtx(&v[4], x0, y0 + leg, c ^ 0x30300000u, x0 / 64.0f, (y0 + leg) / 64.0f);
                vtx(&v[5], x0 + leg, y0 + leg, c ^ 0x30003000u, (x0 + leg) / 64.0f, (y0 + leg) / 64.0f);
                {
                    int j;
                    for (j = 0; j < 6; j++, k++)
                        s->idx[k] = (uint16_t)k;
                }
            }
    }
    if (textured && !make_tex(s, 64, 0, 5, 0))
        return -1;
    if (forced) {
        s->saved_submit = rb_get_submit();
        rb_set_submit(forced - 1);
    }
    s->mesh = rb_mesh_create(s->v, s->nv, s->idx, s->ni);
    if (!s->mesh)
        return -1;
    t->tris = (double)s->copies * cols * rows * 2;
    t->pixels = t->tris * area;
    snprintf(t->note, sizeof t->note, "copies=%d grid=%dx%d", s->copies, cols, rows);
    return 0;
}

static void tri_frame(tctx *t, int f)
{
    synth *s = (synth *)t->p;
    rb_state st;
    int c;
    (void)f;
    rb_clear(0x101010);
    memset(&st, 0, sizeof st);
    if (s->ntex) {
        st.tex = RB_TEX_MODULATE;
        st.bilinear = 1;
        rb_tex_bind(s->tex[0]);
    }
    rb_set_state(&st);
    for (c = 0; c < s->copies; c++) {
        pixel_matrices((float)(c % 4) * 3.0f, (float)(c / 4 % 4) * 2.0f);
        rb_mesh_draw(s->mesh);
    }
    rb_tex_bind(NULL);
}

/* ---- S3: texture upload and working set ----------------------------------
 * param 0: upload rate (4 uploads of a 256x256 texture per frame, each drawn);
 * param N: working set of N 256x256 mipmapped textures, one 64x64 quad each. */
static int tex_setup(tctx *t)
{
    static const uint32_t white[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    synth *s = alloc_synth(t);
    int n = t->def->param, i, q;
    if (!s)
        return -1;
    q = n ? n : (db.quick ? 1 : 4);
    s->v = (rb_vertex *)calloc((size_t)q * 4, sizeof *s->v);
    s->idx = (uint16_t *)calloc((size_t)q * 6, sizeof *s->idx);
    if (!s->v || !s->idx)
        return -1;
    for (i = 0; i < q; i++) {
        float x = 16.0f + (i % 8) * 76.0f, y = 16.0f + (i / 8) * 76.0f;
        quad(&s->v[i * 4], x, y, x + 64, y + 64, white, 64.0f);
        quad_idx(&s->idx[i * 6], 0);
    }
    s->chunk = q;
    if (!n) {
        s->img[0] = (uint8_t *)malloc(256 * 256 * 4);
        s->img[1] = (uint8_t *)malloc(256 * 256 * 4);
        if (!s->img[0] || !s->img[1])
            return -1;
        db_texture(s->img[0], 256, 256, 0, 7);
        db_texture(s->img[1], 256, 256, 0, 8);
        if (!make_tex(s, 256, 0, 7, 0))
            return -1;
        t->texels = (double)q * 256 * 256;
    } else {
        for (i = 0; i < n; i++)
            if (!make_tex(s, 256, 0, 20 + i, RB_TF_MIPMAP))
                return -1;
    }
    t->tris = 2.0 * q;
    t->pixels = 64.0 * 64 * q;
    return 0;
}

static void tex_frame(tctx *t, int f)
{
    synth *s = (synth *)t->p;
    rb_state st;
    int i;
    rb_clear(0x102030);
    pixel_matrices(0, 0);
    memset(&st, 0, sizeof st);
    st.tex = RB_TEX_REPLACE;
    st.bilinear = 1;
    rb_set_state(&st);
    for (i = 0; i < s->chunk; i++) {
        if (t->def->param == 0) {
            rb_tex_update(s->tex[0], s->img[(f + i) & 1]);
            rb_tex_bind(s->tex[0]);
        } else {
            rb_tex_bind(s->tex[i]);
        }
        rb_draw(&s->v[i * 4], 4, s->idx, 6);
    }
    rb_tex_bind(NULL);
}

/* ---- S3SUB: sub-image updates -------------------------------------------
 * A 128x128 texture (a lightmap page) drawn as 16 quads, with its 16 32x32
 * rectangles replaced every frame. param 0: all rectangles replaced, then
 * all quads drawn (GLQuake's lightmaps); param 1: each rectangle replaced
 * just before its quad is drawn (Quake 2 refilling one dynamic lightmap). */
static int sub_setup(tctx *t)
{
    static const uint32_t white[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    synth *s = alloc_synth(t);
    int i, k;
    if (!s)
        return -1;
    s->v = (rb_vertex *)calloc(16 * 4, sizeof *s->v);
    s->idx = (uint16_t *)calloc(6, sizeof *s->idx);
    s->img[0] = (uint8_t *)malloc(32 * 32 * 4 * 16);       /* 16 rectangles, two looks */
    s->img[1] = (uint8_t *)malloc(32 * 32 * 4 * 16);
    if (!s->v || !s->idx || !s->img[0] || !s->img[1] || !make_tex(s, 128, 0, 30, RB_TF_CLAMP))
        return -1;
    for (i = 0; i < 16; i++) {
        float x = 16.0f + (i % 8) * 76.0f, y = 16.0f + (i / 8) * 76.0f;
        rb_vertex *v = &s->v[i * 4];                       /* each quad shows the whole page */
        vtx(&v[0], x, y, white[0], 0, 0);
        vtx(&v[1], x + 64, y, white[1], 1, 0);
        vtx(&v[2], x + 64, y + 64, white[2], 1, 1);
        vtx(&v[3], x, y + 64, white[3], 0, 1);
        for (k = 0; k < 2; k++)
            db_texture(s->img[k] + i * 32 * 32 * 4, 32, 32, 0, 40 + i * 2 + k);
    }
    quad_idx(s->idx, 0);
    s->mode = t->def->param;
    t->tris = 2.0 * 16;
    t->pixels = 64.0 * 64 * 16;
    t->texels = 32.0 * 32 * 16;
    return 0;
}

static void sub_frame(tctx *t, int f)
{
    synth *s = (synth *)t->p;
    rb_state st;
    int i;
    rb_clear(0x102030);
    pixel_matrices(0, 0);
    memset(&st, 0, sizeof st);
    st.tex = RB_TEX_REPLACE;
    rb_set_state(&st);
    for (i = 0; i < 16; i++) {
        const uint8_t *px = s->img[(f + i) & 1] + i * 32 * 32 * 4;
        rb_tex_update_rect(s->tex[0], (i % 4) * 32, (i / 4) * 32, 32, 32, px);
        if (s->mode == 1) {
            rb_tex_bind(s->tex[0]);
            rb_draw(&s->v[i * 4], 4, s->idx, 6);
        }
    }
    if (s->mode == 0) {
        rb_tex_bind(s->tex[0]);
        for (i = 0; i < 16; i++)
            rb_draw(&s->v[i * 4], 4, s->idx, 6);
    }
    rb_tex_bind(NULL);
}

/* ---- S4: state changes --------------------------------------------------
 * 16-pixel textured triangles drawn in chunks of k; param = k + 1000 * what
 * (0 draw calls only, 1 texture change per chunk, 2 blend change per chunk). */
static int state_setup(tctx *t)
{
    synth *s = alloc_synth(t);
    int n = db.quick ? 1024 : 8192, i, k = t->def->param % 1000;
    float leg = (float)sqrt(32.0), cell = leg + 1;
    int cols = (int)((db.w - 20) / cell), rows = (int)((db.h - 20) / cell);
    if (!s)
        return -1;
    s->chunk = k;
    s->nv = n * 3;
    s->v = (rb_vertex *)calloc((size_t)s->nv, sizeof *s->v);
    s->idx = (uint16_t *)calloc((size_t)k * 3, sizeof *s->idx);
    if (!s->v || !s->idx)
        return -1;
    for (i = 0; i < n; i++) {
        int c = i % (cols * rows);
        float x0 = 10 + (c % cols) * cell, y0 = 10 + (c / cols) * cell;
        rb_vertex *v = &s->v[i * 3];
        vtx(&v[0], x0, y0, 0xFFFFFFFFu, x0 / 64.0f, y0 / 64.0f);
        vtx(&v[1], x0, y0 + leg, 0xFFFFFFFFu, x0 / 64.0f, (y0 + leg) / 64.0f);
        vtx(&v[2], x0 + leg, y0, 0xFFFFFFFFu, (x0 + leg) / 64.0f, y0 / 64.0f);
    }
    for (i = 0; i < k * 3; i++)
        s->idx[i] = (uint16_t)i;
    if (!make_tex(s, 64, 0, 11, 0) || !make_tex(s, 64, 0, 12, 0))
        return -1;
    s->ni = n;                          /* triangles */
    t->tris = n;
    t->pixels = 16.0 * n;
    snprintf(t->note, sizeof t->note, "changes=%d", t->def->param / 1000 ? n / k : 0);
    return 0;
}

static void state_frame(tctx *t, int f)
{
    synth *s = (synth *)t->p;
    int what = t->def->param / 1000, k = s->chunk, c, nchunks = s->ni / k;
    rb_state st;
    (void)f;
    rb_clear(0x201010);
    pixel_matrices(0, 0);
    memset(&st, 0, sizeof st);
    st.tex = RB_TEX_MODULATE;
    st.bilinear = 1;
    rb_set_state(&st);
    rb_tex_bind(s->tex[0]);
    for (c = 0; c < nchunks; c++) {
        if (what == 1)
            rb_tex_bind(s->tex[c & 1]);
        else if (what == 2) {
            st.blend = c & 1 ? RB_BLEND_ALPHA : RB_BLEND_NONE;
            rb_set_state(&st);
        }
        rb_draw(&s->v[c * k * 3], k * 3, s->idx, k * 3);
    }
    rb_tex_bind(NULL);
}

/* What the registry (src/core/tests.json) runs: param picks the variant. */
const test_impl impl_basic = { basic_setup, basic_frame, free_synth };
const test_impl impl_fill = { fill_setup, fill_frame, free_synth };
const test_impl impl_tri = { tri_setup, tri_frame, free_synth };
const test_impl impl_tex = { tex_setup, tex_frame, free_synth };
const test_impl impl_sub = { sub_setup, sub_frame, free_synth };
const test_impl impl_state = { state_setup, state_frame, free_synth };
