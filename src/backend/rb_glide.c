/* rb_glide.c - the render backend on Glide 2.x.
 *
 * The runtime (MGA-Glide's GLIDE2X.OVL, or 3dfx's on a Voodoo) is loaded
 * with MGA-Glide's clean-room LE loader and called through its function
 * table. As Glide games did, this backend transforms, clips and projects on
 * the CPU, so Glide numbers include that cost: each vertex a draw uses is
 * transformed once, triangles wholly inside the guard band go straight to
 * grDrawTriangle, the rest are clipped in homogeneous space. Depth is a
 * W-buffer; screen coordinates are snapped to 1/16 pixel.
 *
 * Textures: Glide exposes one TMU's memory (2 MB on MGA-Glide and on a
 * Voodoo Graphics). Converted texel data stays in system memory; a texture
 * is downloaded when first bound, bump-allocated, and when the TMU is full
 * everything is evicted at once, a simple cache as period engines used. */
#include "rb.h"
#include "clip.h"
#include "gtex.h"
#include "texutil.h"
#include "glbind.h"
#include "hx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rb_tex {
    int w, h, flags, format;
    float ss, ts;
    void *data;                 /* converted chain, largest level first */
    size_t bytes;
    GrTexInfo ti;
    FxU32 addr;
    unsigned epoch;             /* resident when == tex_epoch */
};

struct rb_mesh {
    rb_vertex *v;
    uint16_t *idx;
    int nv, ni;
};

static rb_info info;
static le_module *mod;
static int vsync, is_open;
static float hw, hh, gbx, gby;
static mat4 mvp;
static rb_state st;
static int st_valid;
static float colf[256];

static FxU32 tmu_min, tmu_max, tmu_next;
static unsigned tex_epoch = 1;
static unsigned long tex_sent;
static rb_tex *bound, *sourced;
static unsigned sourced_epoch;
static float cur_ss, cur_ts;

static GrVertex *gv;
static cl_vtx *cv;
static unsigned char *oc;
static uint32_t *stamp, stamp_now;
static int cap;

static uint16_t *readbuf;

#define SNAP ((float)(3L << 18))       /* adding this rounds to 1/16 */

int rb_load(const char *path, char *err, int errlen)
{
    int missing = 0;
    if (!path) {
        FILE *f = fopen("C:\\TEST\\GLIDE2X.OVL", "rb");
        path = f ? "C:\\TEST\\GLIDE2X.OVL" : "GLIDE2X.OVL";
        if (f)
            fclose(f);
    }
    mod = glbind_load(path, &missing, err, (size_t)errlen);
    if (!mod)
        return -1;
    if (missing)
        hx_log("glide: %d exports missing from %s", missing, path);
    return 0;
}

static int resolution(int w, int h)
{
    static const struct { int w, h, code; } modes[] = {
        { 320, 240, GR_RESOLUTION_320x240 }, { 400, 300, GR_RESOLUTION_400x300 },
        { 512, 384, GR_RESOLUTION_512x384 }, { 640, 400, GR_RESOLUTION_640x400 },
        { 640, 480, GR_RESOLUTION_640x480 }, { 800, 600, GR_RESOLUTION_800x600 },
        { 1024, 768, GR_RESOLUTION_1024x768 }, { 0, 0, 0 },
    };
    int i;
    for (i = 0; modes[i].w; i++)
        if (modes[i].w == w && modes[i].h == h)
            return modes[i].code;
    return -1;
}

int rb_open(int w, int h, int vs, char *err, int errlen)
{
    GrHwConfiguration hwc;
    char ver[80];
    int res = resolution(w, h), i;
    if (res < 0) {
        snprintf(err, (size_t)errlen, "no Glide resolution for %dx%d", w, h);
        return -1;
    }
    gl.grGlideInit();
    memset(&hwc, 0, sizeof hwc);
    if (!gl.grSstQueryHardware(&hwc) || hwc.num_sst < 1) {
        snprintf(err, (size_t)errlen, "grSstQueryHardware found no hardware");
        return -1;
    }
    gl.grSstSelect(0);
    if (!gl.grSstWinOpen(0, res, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        snprintf(err, (size_t)errlen, "grSstWinOpen %dx%d failed", w, h);
        gl.grGlideShutdown();
        return -1;
    }
    is_open = 1;
    memset(ver, 0, sizeof ver);
    gl.grGlideGetVersion(ver);
    memset(&info, 0, sizeof info);
    info.api = "glide";
    snprintf(info.impl, sizeof info.impl, "Glide %s", ver);
    snprintf(info.card, sizeof info.card, "sst%d fb=%dMB tmu=%dMB", (int)hwc.SSTs[0].type,
             hwc.SSTs[0].sstBoard.VoodooConfig.fbRam, hwc.SSTs[0].sstBoard.VoodooConfig.tmuConfig[0].tmuRam);
    info.width = w;
    info.height = h;
    info.max_tex = 256;
    tmu_min = gl.grTexMinAddress(GR_TMU0);
    tmu_max = gl.grTexMaxAddress(GR_TMU0);
    tmu_next = tmu_min;
    tex_epoch++;
    sourced = NULL;
    bound = NULL;
    info.tex_mem = tmu_max - tmu_min;
    vsync = vs;
    hw = w * 0.5f;
    hh = h * 0.5f;
    gbx = (2000.0f - hw) / hw;
    gby = (2000.0f - hh) / hh;
    if (gbx > 4) gbx = 4;
    if (gby > 4) gby = 4;
    for (i = 0; i < 256; i++)
        colf[i] = (float)i;
    gl.grHints(GR_HINT_STWHINT, 0);
    gl.grTexCombineFunction(GR_TMU0, GR_TEXTURECOMBINE_DECAL);
    gl.grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
    gl.grDepthBufferFunction(GR_CMP_LEQUAL);
    gl.grDepthMask(FXTRUE);
    gl.grClipWindow(0, 0, (FxU32)w, (FxU32)h);
    st_valid = 0;
    {
        rb_state s;
        memset(&s, 0, sizeof s);
        rb_set_state(&s);
    }
    return 0;
}

void rb_close(void)
{
    if (is_open)
        gl.grGlideShutdown();
    is_open = 0;
    free(readbuf);
    readbuf = NULL;
}

const rb_info *rb_get_info(void) { return &info; }
void rb_set_submit(int mode) { (void)mode; }
int rb_get_submit(void) { return RB_SUBMIT_ARRAYS; }
unsigned long rb_tex_bytes(void) { return tex_sent; }

/* grBufferClear leaves depth alone while depth buffering is disabled or
 * depth writes are masked (as on a Voodoo): enable both for the clear. */
void rb_clear(uint32_t rgb)
{
    int restore = st_valid && !st.depth_write;
    if (restore) {
        gl.grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
        gl.grDepthMask(FXTRUE);
    }
    gl.grBufferClear(rgb & 0xFFFFFFu, 0, GR_WDEPTHVALUE_FARTHEST);
    if (restore) {
        gl.grDepthMask(FXFALSE);
        if (!st.depth_test)
            gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    }
}

static void combine(int tex)
{
    switch (tex) {
    case RB_TEX_MODULATE:
        gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        gl.grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case RB_TEX_REPLACE:
        gl.grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        gl.grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                          GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    default:
        gl.grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_NONE, FXFALSE);
        gl.grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                          GR_COMBINE_OTHER_NONE, FXFALSE);
        break;
    }
}

static void blend(int mode)
{
    switch (mode) {
    case RB_BLEND_ALPHA:
        gl.grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    case RB_BLEND_ADD:
        gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    case RB_BLEND_MUL:
        gl.grAlphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    default:
        gl.grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
        break;
    }
}

/* Apply only what changed, as an engine would. */
void rb_set_state(const rb_state *s)
{
    int all = !st_valid;
    if (all || s->depth_test != st.depth_test || s->depth_write != st.depth_write) {
        if (!s->depth_test && !s->depth_write) {
            gl.grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
        } else {
            gl.grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER);
            gl.grDepthBufferFunction(s->depth_test ? GR_CMP_LEQUAL : GR_CMP_ALWAYS);
            gl.grDepthMask(s->depth_write ? FXTRUE : FXFALSE);
        }
    }
    if (all || s->blend != st.blend)
        blend(s->blend);
    if (all || s->alpha_test != st.alpha_test) {
        gl.grAlphaTestFunction(s->alpha_test ? GR_CMP_GREATER : GR_CMP_ALWAYS);
        gl.grAlphaTestReferenceValue(127);
    }
    if (all || s->fog != st.fog || (s->fog && (s->fog_start != st.fog_start || s->fog_end != st.fog_end ||
                                               s->fog_rgb != st.fog_rgb))) {
        if (s->fog) {
            static GrFog_t table[GR_FOG_TABLE_SIZE];
            gl.guFogGenerateLinear(table, s->fog_start, s->fog_end);
            gl.grFogTable(table);
            gl.grFogColorValue(s->fog_rgb & 0xFFFFFFu);
            gl.grFogMode(GR_FOG_WITH_TABLE);
        } else {
            gl.grFogMode(GR_FOG_DISABLE);
        }
    }
    if (all || s->tex != st.tex)
        combine(s->tex);
    if (all || s->bilinear != st.bilinear) {
        int f = s->bilinear ? GR_TEXTUREFILTER_BILINEAR : GR_TEXTUREFILTER_POINT_SAMPLED;
        gl.grTexFilterMode(GR_TMU0, f, f);
    }
    if (all || s->cull != st.cull)
        /* Screen y grows downwards, so counter-clockwise front faces have
         * negative area in Glide's coordinates. */
        gl.grCullMode(s->cull ? GR_CULL_POSITIVE : GR_CULL_DISABLE);
    st = *s;
    st_valid = 1;
}

void rb_set_matrices(const mat4 *proj, const mat4 *mv)
{
    m4_mul(&mvp, proj, mv);
}

/* ---- Textures -------------------------------------------------------- */
rb_tex *rb_tex_create(int w, int h, const uint8_t *rgba, int flags)
{
    rb_tex *t;
    int grey, cls;
    if (gt_lod(w > h ? w : h) < 0 || gt_aspect(w, h) < 0)
        return NULL;
    t = (rb_tex *)calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->w = w;
    t->h = h;
    t->flags = flags;
    cls = tu_classify(rgba, (long)w * h, &grey);
    t->format = gt_format(cls, grey);
    t->ti.largeLod = gt_lod(w > h ? w : h);
    t->ti.smallLod = flags & RB_TF_MIPMAP ? GR_LOD_1 : t->ti.largeLod;
    t->ti.aspectRatio = gt_aspect(w, h);
    t->ti.format = t->format;
    t->bytes = gl.grTexCalcMemRequired(t->ti.smallLod, t->ti.largeLod, t->ti.aspectRatio, t->format);
    t->data = calloc(1, t->bytes + 64);
    if (!t->data) {
        free(t);
        return NULL;
    }
    t->ti.data = t->data;
    gt_st_scale(w, h, &t->ss, &t->ts);
    rb_tex_update(t, rgba);
    return t;
}

void rb_tex_update(rb_tex *t, const uint8_t *rgba)
{
    uint8_t *lv = NULL, *nx;
    const uint8_t *src = rgba;
    unsigned char *o = (unsigned char *)t->data;
    int w = t->w, h = t->h, levels = t->flags & RB_TF_MIPMAP ? tu_levels(w, h) : 1, l;
    for (l = 0; l < levels; l++) {
        o += gt_convert(src, (long)w * h, t->format, o);
        if (l + 1 < levels) {
            nx = (uint8_t *)malloc((size_t)(w > 1 ? w / 2 : 1) * (h > 1 ? h / 2 : 1) * 4);
            if (!nx)
                break;
            tu_mip(src, w, h, nx);
            free(lv);
            src = lv = nx;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    free(lv);
    if (t->epoch == tex_epoch) {        /* resident: download again in place */
        gl.grTexDownloadMipMap(GR_TMU0, t->addr, GR_MIPMAPLEVELMASK_BOTH, &t->ti);
        tex_sent += (unsigned long)t->bytes;
    }
}

void rb_tex_bind(rb_tex *t)
{
    bound = t;
}

void rb_tex_free(rb_tex *t)
{
    if (!t)
        return;
    if (bound == t)
        bound = NULL;
    if (sourced == t)
        sourced = NULL;
    free(t->data);
    free(t);
}

static int tex_prepare(void)
{
    rb_tex *t = bound;
    if (!t || st.tex == RB_TEX_OFF) {
        cur_ss = cur_ts = 0;
        return 0;
    }
    if (t->epoch != tex_epoch) {
        FxU32 need = (FxU32)((t->bytes + 7) & ~(size_t)7);
        if (tmu_next + need > tmu_max) {
            tex_epoch++;                    /* evict everything */
            tmu_next = tmu_min;
            if (tmu_next + need > tmu_max)
                return -1;
        }
        t->addr = tmu_next;
        t->epoch = tex_epoch;
        tmu_next += need;
        gl.grTexDownloadMipMap(GR_TMU0, t->addr, GR_MIPMAPLEVELMASK_BOTH, &t->ti);
        tex_sent += (unsigned long)t->bytes;
    }
    if (sourced != t || sourced_epoch != tex_epoch) {
        gl.grTexSource(GR_TMU0, t->addr, GR_MIPMAPLEVELMASK_BOTH, &t->ti);
        gl.grTexMipMapMode(GR_TMU0, t->flags & RB_TF_MIPMAP ? GR_MIPMAP_NEAREST : GR_MIPMAP_DISABLE, FXFALSE);
        gl.grTexClampMode(GR_TMU0, t->flags & RB_TF_CLAMP ? GR_TEXTURECLAMP_CLAMP : GR_TEXTURECLAMP_WRAP,
                          t->flags & RB_TF_CLAMP ? GR_TEXTURECLAMP_CLAMP : GR_TEXTURECLAMP_WRAP);
        sourced = t;
        sourced_epoch = tex_epoch;
    }
    cur_ss = t->ss;
    cur_ts = t->ts;
    return 0;
}

/* ---- Geometry -------------------------------------------------------- */
static int scratch(int n)
{
    if (n <= cap)
        return 0;
    free(gv); free(cv); free(oc); free(stamp);
    cap = n + 256;
    gv = (GrVertex *)malloc((size_t)cap * sizeof *gv);
    cv = (cl_vtx *)malloc((size_t)cap * sizeof *cv);
    oc = (unsigned char *)malloc((size_t)cap);
    stamp = (uint32_t *)calloc((size_t)cap, sizeof *stamp);
    stamp_now = 0;
    if (!gv || !cv || !oc || !stamp) {
        cap = 0;
        return -1;
    }
    return 0;
}

static void project(GrVertex *g, const cl_vtx *c)
{
    volatile float sx, sy;
    float oow = 1.0f / c->w;
    sx = c->x * oow * hw + hw + SNAP;
    sy = hh - c->y * oow * hh + SNAP;
    g->x = sx - SNAP;
    g->y = sy - SNAP;
    g->z = 0;
    g->ooz = (c->z * oow * 0.5f + 0.5f) * 65535.0f;
    g->oow = oow;
    g->r = c->r; g->g = c->g; g->b = c->b; g->a = c->a;
    g->tmuvtx[0].sow = c->u * cur_ss * oow;
    g->tmuvtx[0].tow = c->v * cur_ts * oow;
    g->tmuvtx[0].oow = oow;
}

static void draw_clipped(const cl_vtx *a, const cl_vtx *b, const cl_vtx *c, unsigned mask)
{
    cl_vtx in[3], out[CL_MAX_OUT];
    GrVertex g[CL_MAX_OUT];
    int n, i;
    in[0] = *a; in[1] = *b; in[2] = *c;
    n = cl_polygon(in, 3, out, mask, gbx, gby);
    for (i = 0; i < n; i++)
        project(&g[i], &out[i]);
    for (i = 1; i + 1 < n; i++)
        gl.grDrawTriangle(&g[0], &g[i], &g[i + 1]);
}

static void xform(const rb_vertex *p, int i)
{
    const float *m = mvp.m;
    cl_vtx *c = &cv[i];
    unsigned o = 0;
    float x = p->x, y = p->y, z = p->z, gw;
    c->x = m[0] * x + m[4] * y + m[8] * z + m[12];
    c->y = m[1] * x + m[5] * y + m[9] * z + m[13];
    c->z = m[2] * x + m[6] * y + m[10] * z + m[14];
    c->w = m[3] * x + m[7] * y + m[11] * z + m[15];
    c->r = colf[p->c[0]]; c->g = colf[p->c[1]]; c->b = colf[p->c[2]]; c->a = colf[p->c[3]];
    c->u = st.uvset ? p->u2 : p->u;
    c->v = st.uvset ? p->v2 : p->v;
    if (c->z < -c->w) o |= CL_NEAR;
    if (c->z > c->w) o |= CL_FAR;
    gw = gbx * c->w;
    if (c->x < -gw) o |= CL_LEFT;
    if (c->x > gw) o |= CL_RIGHT;
    gw = gby * c->w;
    if (c->y < -gw) o |= CL_BOTTOM;
    if (c->y > gw) o |= CL_TOP;
    oc[i] = (unsigned char)o;
    if (!o)
        project(&gv[i], c);
}

/* Each vertex is transformed once, when a triangle first uses it, so a draw
 * that references a few vertices of a large array (visible faces of a level
 * batch) pays only for those. */
void rb_draw(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    int i;
    if (nv <= 0 || scratch(nv) < 0 || tex_prepare() < 0)
        return;
    if (++stamp_now == 0) {
        memset(stamp, 0, (size_t)cap * sizeof *stamp);
        stamp_now = 1;
    }
    for (i = 0; i + 2 < ni; i += 3) {
        unsigned a = idx[i], b = idx[i + 1], c = idx[i + 2], o;
        if (stamp[a] != stamp_now) { stamp[a] = stamp_now; xform(&v[a], (int)a); }
        if (stamp[b] != stamp_now) { stamp[b] = stamp_now; xform(&v[b], (int)b); }
        if (stamp[c] != stamp_now) { stamp[c] = stamp_now; xform(&v[c], (int)c); }
        o = oc[a] | oc[b] | oc[c];
        if (!o)
            gl.grDrawTriangle(&gv[a], &gv[b], &gv[c]);
        else if (!(oc[a] & oc[b] & oc[c]))
            draw_clipped(&cv[a], &cv[b], &cv[c], o);
    }
}

rb_mesh *rb_mesh_create(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    rb_mesh *m = (rb_mesh *)calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->v = (rb_vertex *)malloc((size_t)nv * sizeof *v);
    m->idx = (uint16_t *)malloc((size_t)ni * sizeof *idx);
    if (!m->v || !m->idx) {
        rb_mesh_free(m);
        return NULL;
    }
    memcpy(m->v, v, (size_t)nv * sizeof *v);
    memcpy(m->idx, idx, (size_t)ni * sizeof *idx);
    m->nv = nv;
    m->ni = ni;
    return m;
}

void rb_mesh_draw(rb_mesh *m)
{
    rb_draw(m->v, m->nv, m->idx, m->ni);
}

void rb_mesh_free(rb_mesh *m)
{
    if (!m)
        return;
    free(m->v);
    free(m->idx);
    free(m);
}

void rb_swap(void)
{
    gl.grBufferSwap(vsync ? 1 : 0);
}

void rb_finish(void)
{
    gl.grSstIdle();
}

int rb_read(uint8_t *rgb)
{
    long i, n = (long)info.width * info.height;
    if (!readbuf)
        readbuf = (uint16_t *)malloc((size_t)n * 2);
    if (!readbuf)
        return -1;
    gl.grSstIdle();
    if (!gl.grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, (FxU32)info.width, (FxU32)info.height,
                            (FxU32)info.width * 2, readbuf))
        return -1;
    for (i = 0; i < n; i++) {
        unsigned p = readbuf[i], r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        rgb[i * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
        rgb[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
        rgb[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
    }
    return 0;
}
