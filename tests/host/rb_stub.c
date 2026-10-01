/* rb_stub.c - rb.h for the host: draws nothing, measures what would be drawn.
 *
 * Every call's inputs (state, matrices, texture names, vertices, indices,
 * clear colours) go into a 64-bit FNV-1a hash per frame, so two runs of a
 * test draw the same frame exactly when their hashes match. It also counts,
 * per frame: triangles submitted, triangles on screen (front-facing when
 * culling, not clipped away), draw calls, texture binds, and the pixels
 * they would cover: each triangle clipped against the near plane and the
 * screen, with no depth rejection, so an upper bound on fill
 * (tests/host/screplay.c reports them). */
#include "rb.h"
#include "rb_stub.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct rb_tex { int id, w, h; };
struct rb_mesh { rb_vertex *v; uint16_t *idx; int nv, ni; };

static rb_info info = { "stub", "rb_stub", "none", 640, 480, 2u << 20, 640, 480, "native", 256 };
static rb_state cur;
static mat4 proj, mv, clip;
static int ntex_made, bound;
static unsigned long tex_bytes;
rb_stub_stats rb_stub;

static void eat(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        rb_stub.hash = (rb_stub.hash ^ *b++) * 0x100000001B3ull;
}

static void eat_i(int op, int v) { eat(&op, sizeof op); eat(&v, sizeof v); }

void rb_stub_frame(void)
{
    memset(&rb_stub, 0, sizeof rb_stub);
    rb_stub.hash = 0xCBF29CE484222325ull;
}

void rb_stub_size(int w, int h)
{
    info.width = info.display_w = w;
    info.height = info.display_h = h;
}

int rb_load(const char *path, char *err, int errlen) { (void)path; (void)err; (void)errlen; return 0; }
int rb_open(int w, int h, int vsync, char *err, int errlen)
{
    (void)vsync; (void)err; (void)errlen;
    rb_stub_size(w, h);
    return 0;
}
void rb_close(void) {}
const rb_info *rb_get_info(void) { return &info; }
void rb_set_submit(int mode) { (void)mode; }
int rb_get_submit(void) { return RB_SUBMIT_ARRAYS; }

void rb_clear(uint32_t rgb)
{
    eat_i(1, (int)rgb);
    rb_stub.fill += (double)info.width * info.height;
}

void rb_set_state(const rb_state *s)
{
    int f[11];
    float g[2];
    cur = *s;
    f[0] = s->depth_test; f[1] = s->depth_write; f[2] = s->blend; f[3] = s->alpha_test; f[4] = s->fog;
    f[5] = (int)s->fog_rgb; f[6] = s->tex; f[7] = s->bilinear; f[8] = s->cull; f[9] = s->uvset; f[10] = 2;
    g[0] = s->fog ? s->fog_start : 0;
    g[1] = s->fog ? s->fog_end : 0;
    eat(f, sizeof f);
    eat(g, sizeof g);
    rb_stub.states++;
}

void rb_set_matrices(const mat4 *p, const mat4 *m)
{
    proj = *p;
    mv = *m;
    m4_mul(&clip, &proj, &mv);
    eat_i(3, 0);
    eat(p->m, sizeof p->m);
    eat(m->m, sizeof m->m);
    rb_stub.matrices++;
}

rb_tex *rb_tex_create(int w, int h, const uint8_t *rgba, int flags)
{
    rb_tex *t = (rb_tex *)calloc(1, sizeof *t);
    (void)rgba; (void)flags;
    if (!t)
        return NULL;
    t->id = ++ntex_made;
    t->w = w;
    t->h = h;
    tex_bytes += (unsigned long)w * h * 2;
    return t;
}
void rb_tex_update(rb_tex *t, const uint8_t *rgba) { (void)rgba; eat_i(4, t ? t->id : 0); tex_bytes += t ? (unsigned long)t->w * t->h * 2 : 0; }
void rb_tex_update_rect(rb_tex *t, int x, int y, int w, int h, const uint8_t *rgba)
{
    (void)x; (void)y; (void)rgba;
    eat_i(5, t ? t->id : 0);
    tex_bytes += (unsigned long)w * h * 2;
}
void rb_tex_bind(rb_tex *t)
{
    int id = t ? t->id : 0;
    if (id != bound)
        rb_stub.binds++;
    bound = id;
    eat_i(6, id);
}
void rb_tex_free(rb_tex *t) { free(t); }
unsigned long rb_tex_bytes(void) { return tex_bytes; }

/* Clip-space vertex. */
typedef struct { float x, y, z, w; } cv;

static cv lerp(cv a, cv b, float t)
{
    cv r;
    r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t; r.w = a.w + (b.w - a.w) * t;
    return r;
}

/* Pixels a clip-space triangle covers on screen; sets *front. */
static double tri_area(cv a, cv b, cv c, int *front)
{
    cv in[3], poly[4];
    float px[12][2], q[12][2];
    int n = 0, i, k, m;
    double area = 0;
    in[0] = a; in[1] = b; in[2] = c;
    /* Near plane: z > -w. */
    for (i = 0; i < 3; i++) {
        cv p = in[i], s = in[(i + 1) % 3];
        float dp = p.z + p.w, ds = s.z + s.w;
        if (dp >= 0)
            poly[n++] = p;
        if ((dp >= 0) != (ds >= 0) && n < 4)
            poly[n++] = lerp(p, s, dp / (dp - ds));
    }
    *front = 0;
    if (n < 3)
        return 0;
    for (i = 0; i < n; i++) {
        float iw = poly[i].w > 1e-6f ? 1.0f / poly[i].w : 1e6f;
        px[i][0] = (poly[i].x * iw * 0.5f + 0.5f) * info.width;
        px[i][1] = (0.5f - poly[i].y * iw * 0.5f) * info.height;     /* y down */
    }
    for (i = 0; i < n; i++)                   /* signed area, y down: clockwise on screen is front */
        area += (double)px[i][0] * px[(i + 1) % n][1] - (double)px[(i + 1) % n][0] * px[i][1];
    *front = area < 0;
    /* Clip to the screen, edge by edge. */
    for (k = 0; k < 4; k++) {
        int axis = k & 1, hi = k >> 1;
        float lim = hi ? (axis ? (float)info.height : (float)info.width) : 0.0f;
        m = 0;
        for (i = 0; i < n; i++) {
            float *p = px[i], *s = px[(i + 1) % n];
            int pin = hi ? p[axis] <= lim : p[axis] >= lim, sin_ = hi ? s[axis] <= lim : s[axis] >= lim;
            if (pin && m < 12) {
                q[m][0] = p[0]; q[m][1] = p[1]; m++;
            }
            if (pin != sin_ && m < 12) {
                float t = (lim - p[axis]) / (s[axis] - p[axis]);
                q[m][0] = p[0] + (s[0] - p[0]) * t;
                q[m][1] = p[1] + (s[1] - p[1]) * t;
                m++;
            }
        }
        n = m;
        memcpy(px, q, sizeof(float) * 2 * (size_t)m);
        if (n < 3)
            return 0;
    }
    area = 0;
    for (i = 0; i < n; i++)
        area += (double)px[i][0] * px[(i + 1) % n][1] - (double)px[(i + 1) % n][0] * px[i][1];
    return fabs(area) * 0.5;
}

static void measure(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    int i;
    eat_i(7, nv);
    eat(v, sizeof *v * (size_t)nv);
    eat(idx, sizeof *idx * (size_t)ni);
    rb_stub.draws++;
    rb_stub.tris += ni / 3;
    for (i = 0; i + 2 < ni; i += 3) {
        cv c[3];
        int k, front;
        double a;
        for (k = 0; k < 3; k++) {
            float o[4], p[3];
            const rb_vertex *s = &v[idx[i + k]];
            p[0] = s->x; p[1] = s->y; p[2] = s->z;
            m4_xform(&clip, p, o);
            c[k].x = o[0]; c[k].y = o[1]; c[k].z = o[2]; c[k].w = o[3];
        }
        a = tri_area(c[0], c[1], c[2], &front);
        if (a > 0 && (front || !cur.cull)) {
            rb_stub.tris_shown++;
            rb_stub.fill += a;
            if (cur.blend != RB_BLEND_NONE)
                rb_stub.fill_blend += a;
        }
    }
}

void rb_draw(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    measure(v, nv, idx, ni);
}

rb_mesh *rb_mesh_create(const rb_vertex *v, int nv, const uint16_t *idx, int ni)
{
    rb_mesh *m = (rb_mesh *)calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->v = (rb_vertex *)malloc(sizeof *v * (size_t)(nv ? nv : 1));
    m->idx = (uint16_t *)malloc(sizeof *idx * (size_t)(ni ? ni : 1));
    if (!m->v || !m->idx)
        return NULL;
    memcpy(m->v, v, sizeof *v * (size_t)nv);
    memcpy(m->idx, idx, sizeof *idx * (size_t)ni);
    m->nv = nv;
    m->ni = ni;
    return m;
}
void rb_mesh_draw(rb_mesh *m) { measure(m->v, m->nv, m->idx, m->ni); }
void rb_mesh_free(rb_mesh *m)
{
    if (m) {
        free(m->v);
        free(m->idx);
        free(m);
    }
}

void rb_swap(void) {}
void rb_finish(void) {}
int rb_read(uint8_t *rgb) { memset(rgb, 0, (size_t)info.width * info.height * 3); return 0; }
