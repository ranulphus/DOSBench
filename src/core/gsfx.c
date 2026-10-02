/* gsfx.c - particles and per-frame vertex work for the game scenes (gs.h).
 *
 * A particle is never stored: emitter e's k-th particle is born at
 * f0/rate + k/rate_e, from where the emitter was then, in a direction and
 * at a speed drawn from gs_rand(seed, k, channel); where it is at time t
 * follows in closed form (drag, gravity, rise). A burst's particles are all
 * born at its frame. So any frame can be drawn on its own, in any order. */
#include "gs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CH_LIFE, CH_SPEED, CH_CONE1, CH_CONE2, CH_SPIN, CH_FLICKER };

static float life_max(const sc_part *P)
{
    return P->life * (1.0f + P->life_jitter);
}

int gsfx_capacity(const scene *sc)
{
    const sc_ghdr *g = sc->ghdr;
    long total = 0, most = 0;
    int i, f;
    for (i = 0; i < sc->nemit; i++)
        total += (long)ceil(sc->emit[i].rate * life_max(&sc->part[sc->emit[i].part])) + 2;
    for (f = 0; sc->nfxev && f < (int)g->frames; f++) {
        long n = 0;
        for (i = 0; i < sc->nfxev; i++) {
            const sc_fxev *e = &sc->fxev[i];
            double end = e->frame + life_max(&sc->part[e->part]) * g->rate + 1;
            if ((uint32_t)f >= e->frame && f < end)
                n += (long)e->count;
        }
        if (n > most)
            most = n;
    }
    total += most;
    return (int)(total > 30000 ? 30000 : total);
}

/* A unit direction within spread radians of d. */
static void cone(const float d[3], float spread, float u1, float u2, float out[3])
{
    float a[3] = { 0, 1, 0 }, b[3], c[3], ct, st, phi;
    int k;
    if (fabs(d[1]) > 0.9f) {
        a[0] = 1; a[1] = 0;
    }
    v3_cross(b, a, d);
    v3_norm(b);
    v3_cross(c, d, b);
    ct = spread >= (float)VM_PI ? 1 - 2 * u1 : 1 - u1 * (1 - (float)cos(spread));
    st = (float)sqrt(ct < 1 ? 1 - ct * ct : 0);
    phi = (float)(2 * VM_PI) * u2;
    for (k = 0; k < 3; k++)
        out[k] = d[k] * ct + (b[k] * (float)cos(phi) + c[k] * (float)sin(phi)) * st;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    uint32_t r = 0;
    int s;
    for (s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
        r |= (uint32_t)(x + (y - x) * t + 0.5f) << s;
    }
    return r;
}

/* Particle k of a source born at tb from origin going along dir; added if alive at w->t and in view. */
static void particle(gs_world *w, int part, uint32_t seed, uint32_t k, double tb, const float origin[3],
                     const float dir[3], float scale)
{
    const sc_part *P = &w->sc->part[part];
    gs_particle *q;
    double age = w->t - tb;
    float life = P->life * (1 + P->life_jitter * (2 * gs_rand(seed, k, CH_LIFE) - 1)), a, s, v[3], d[3], vd[3];
    float speed, alpha;
    uint32_t rgba;
    int i;
    if (age < 0 || age >= life || w->np >= w->pmax)
        return;
    a = (float)(age / life);
    speed = P->speed * scale * (1 + P->speed_jitter * (2 * gs_rand(seed, k, CH_SPEED) - 1));
    cone(dir, P->spread, gs_rand(seed, k, CH_CONE1), gs_rand(seed, k, CH_CONE2), v);
    s = P->drag > 0 ? (float)((1 - exp(-P->drag * age)) / P->drag) : (float)age;
    q = &w->pt[w->np];
    for (i = 0; i < 3; i++)
        q->pos[i] = origin[i] + v[i] * speed * s;
    q->pos[1] += (float)(P->rise * age - 0.5 * P->gravity * age * age);
    q->size = (P->size0 + (P->size1 - P->size0) * a) * scale;
    q->angle = P->spin * (2 * gs_rand(seed, k, CH_SPIN) - 1) * (float)age;
    rgba = mix(P->rgba0, P->rgba1, a);
    if (P->fade_in > 0 && a < P->fade_in) {
        alpha = (float)(rgba & 255) * a / P->fade_in;
        rgba = (rgba & 0xFFFFFF00u) | (uint32_t)alpha;
    }
    q->rgba = rgba;
    q->part = (uint16_t)part;
    q->flat = (P->flags & SC_PF_FLAT) != 0;
    v3_sub(d, q->pos, w->eye);
    v3_sub(vd, w->at, w->eye);
    v3_norm(vd);
    q->depth = v3_dot(d, vd);
    if (q->depth < -q->size || !cull_sphere(&w->fr, q->pos, q->size))
        return;
    w->np++;
}

/* An emitter's origin and direction in the world at time t (0 if its instance is not there). */
static int source(const gs_world *w, uint16_t inst, const float pos[3], const float dir[3], double t,
                  float o[3], float d[3])
{
    mat4 m;
    float p4[4];
    if (inst == SC_NONE) {
        memcpy(o, pos, 12);
        memcpy(d, dir, 12);
        v3_norm(d);
        return 1;
    }
    if (!gs_inst_at(w->sc, inst, t, &m))
        return 0;
    m4_xform(&m, pos, p4);
    o[0] = p4[0]; o[1] = p4[1]; o[2] = p4[2];
    d[0] = m.m[0] * dir[0] + m.m[4] * dir[1] + m.m[8] * dir[2];
    d[1] = m.m[1] * dir[0] + m.m[5] * dir[1] + m.m[9] * dir[2];
    d[2] = m.m[2] * dir[0] + m.m[6] * dir[1] + m.m[10] * dir[2];
    v3_norm(d);
    return 1;
}

void gsfx_particles(gs_world *w)
{
    const scene *sc = w->sc;
    double rate = w->g->rate;
    int i;
    w->np = 0;
    for (i = 0; i < sc->nemit; i++) {
        const sc_emit *e = &sc->emit[i];
        const sc_part *P = &sc->part[e->part];
        double t0 = e->f0 / rate, t1 = e->f1 ? e->f1 / rate : 1e30, lm = life_max(P);
        long k0, k1, k;
        float o[3], d[3];
        uint32_t seed = e->seed ^ (uint32_t)(i * 7919);
        int now = 0;
        if (w->t < t0)
            continue;
        k0 = (long)ceil((w->t - lm - t0) * e->rate);
        if (k0 < 0)
            k0 = 0;
        k1 = (long)floor((w->t - t0) * e->rate);
        if (e->flags & SC_EF_MOVE)
            now = source(w, e->inst, e->pos, e->dir, w->t, o, d);
        for (k = k0; k <= k1; k++) {
            double tb = t0 + k / e->rate;
            if (tb >= t1)
                break;
            if (e->flags & SC_EF_MOVE) {
                if (now)
                    particle(w, e->part, seed, (uint32_t)k, tb, o, d, 1.0f);
            } else if (source(w, e->inst, e->pos, e->dir, tb, o, d))
                particle(w, e->part, seed, (uint32_t)k, tb, o, d, 1.0f);
        }
    }
    for (i = 0; i < sc->nfxev; i++) {
        const sc_fxev *e = &sc->fxev[i];
        double tb = e->frame / rate;
        float o[3], d[3], up[3] = { 0, 1, 0 };
        uint32_t k;
        if (w->t < tb || w->t >= tb + life_max(&sc->part[e->part]))
            continue;
        if (!source(w, e->inst, e->pos, up, tb, o, d))
            continue;
        for (k = 0; k < e->count; k++)
            particle(w, e->part, e->seed ^ (uint32_t)(i * 104729), k, tb, o, d, e->scale > 0 ? e->scale : 1.0f);
    }
}

static const gs_particle *sort_pt;
static int far_first(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (sort_pt[x].depth != sort_pt[y].depth)
        return sort_pt[x].depth > sort_pt[y].depth ? -1 : 1;
    return x - y;
}

static void part_state(const gs_world *w, const sc_part *P, rb_state *st)
{
    db_state_default(st);
    st->depth_write = 0;
    st->bilinear = 1;
    st->tex = P->tex != SC_NONE ? RB_TEX_MODULATE : RB_TEX_OFF;
    st->blend = P->flags & SC_PF_ADD ? RB_BLEND_ADD : RB_BLEND_ALPHA;
    st->alpha_test = (P->flags & SC_PF_ALPHATEST) != 0;
    if ((w->g->flags & SC_GF_FOG) && !(P->flags & SC_PF_NOFOG)) {
        st->fog = 1;
        st->fog_start = w->g->fog_start;
        st->fog_end = w->g->fog_end;
        st->fog_rgb = P->flags & SC_PF_ADD ? 0 : w->g->fog_rgb;
    }
}

void gsfx_draw_particles(gs_world *w, int additive)
{
    const scene *sc = w->sc;
    float r[3], u[3];
    int n = 0, i, run = 0;
    for (i = 0; i < w->np; i++)
        if (((sc->part[w->pt[i].part].flags & SC_PF_ADD) != 0) == additive)
            w->porder[n++] = i;
    if (!n)
        return;
    if (!additive) {
        sort_pt = w->pt;
        qsort(w->porder, (size_t)n, sizeof *w->porder, far_first);
    }
    cull_view_axes(&w->view, r, u);
    for (i = 0; i <= n; i++) {
        const gs_particle *q = i < n ? &w->pt[w->porder[i]] : NULL;
        if (run && (!q || q->part != w->pt[w->porder[i - 1]].part || run == 4096)) {
            const sc_part *P = &sc->part[w->pt[w->porder[i - 1]].part];
            rb_state st;
            part_state(w, P, &st);
            gs_set_state(w, &st);
            gs_bind(w, P->tex != SC_NONE ? sc->rtex[P->tex] : NULL);
            rb_draw(w->pv, run * 4, w->pi, run * 6);
            w->tris += 2.0 * run;
            run = 0;
        }
        if (q) {
            rb_vertex *v = w->pv + run * 4;
            float c = (float)cos(q->angle * VM_PI / 180.0), s = (float)sin(q->angle * VM_PI / 180.0);
            float ax[3], ay[3], h = q->size * 0.5f;
            int j, k;
            for (k = 0; k < 3; k++) {
                float rr = q->flat ? (k == 0) : r[k], uu = q->flat ? (k == 2) : u[k];
                ax[k] = (rr * c + uu * s) * h;
                ay[k] = (uu * c - rr * s) * h;
            }
            for (j = 0; j < 4; j++) {
                float sx = (j == 1 || j == 2) ? 1.0f : -1.0f, sy = j >= 2 ? 1.0f : -1.0f;
                v[j].x = q->pos[0] + ax[0] * sx + ay[0] * sy;
                v[j].y = q->pos[1] + ax[1] * sx + ay[1] * sy;
                v[j].z = q->pos[2] + ax[2] * sx + ay[2] * sy;
                v[j].c[0] = (uint8_t)(q->rgba >> 24);
                v[j].c[1] = (uint8_t)(q->rgba >> 16);
                v[j].c[2] = (uint8_t)(q->rgba >> 8);
                v[j].c[3] = (uint8_t)q->rgba;
                v[j].u = v[j].u2 = (j == 1 || j == 2) ? 1.0f : 0.0f;
                v[j].v = v[j].v2 = j >= 2 ? 0.0f : 1.0f;
            }
            run++;
        }
    }
}

/* ---- Vertices worked on per frame ------------------------------------------ */
static float flicker(uint32_t seed, double x)
{
    double i = floor(x);
    float f = (float)(x - i), a = gs_rand(seed, (uint32_t)(long)i, CH_FLICKER),
          b = gs_rand(seed, (uint32_t)(long)i + 1, CH_FLICKER);
    f = f * f * (3 - 2 * f);
    return (a + (b - a) * f) * 2 - 1;
}

static uint8_t scale8(uint8_t c, float k)
{
    float v = c * k + 0.5f;
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

void gsfx_vertices(gs_world *w, int inst, int batch, const uint16_t *lut)
{
    const scene *sc = w->sc;
    const sc_batch *b = &sc->batch[batch];
    const sc_model *md = &sc->model[inst >= 0 ? w->drawn[inst] : (uint16_t)w->g->sky];
    rb_vertex *v = w->vbuf;
    const sc_anim *an = NULL;
    const uint8_t *f0 = NULL, *f1 = NULL;
    float fr = 0;
    double t = w->t;
    uint32_t j;
    memcpy(v, sc->vert + b->vfirst, sizeof *v * b->vcount);
    if ((md->flags & SC_MF_ANIM) && inst >= 0) {
        const sc_inst *n = &sc->inst[inst];
        double ft = (t - n->f0 / w->g->rate + n->anim[3]) * n->anim[2];
        long cnt = n->anim[1] >= 1 ? (long)n->anim[1] : 1, i0 = (long)floor(ft), i1;
        an = &sc->anim[md->anim];
        fr = (float)(ft - i0);
        i0 = ((i0 % cnt) + cnt) % cnt;
        i1 = (i0 + 1) % cnt;
        i0 += (long)n->anim[0];
        i1 += (long)n->anim[0];
        if (i0 >= (long)an->nframes) i0 = (long)an->nframes - 1;
        if (i1 >= (long)an->nframes) i1 = (long)an->nframes - 1;
        f0 = an->data + (size_t)i0 * an->nverts * 4;
        f1 = an->data + (size_t)i1 * an->nverts * 4;
    }
    for (j = 0; j < b->vcount; j++) {
        uint32_t gv = b->vfirst + j;
        int nidx = sc->vnormal ? sc->vnormal[gv] : 0;
        if (an && gv >= an->vfirst && gv < an->vfirst + an->nverts) {
            const uint8_t *p = f0 + (gv - an->vfirst) * 4, *q = f1 + (gv - an->vfirst) * 4;
            int k;
            float *dst = &v[j].x;
            for (k = 0; k < 3; k++)
                dst[k] = an->origin[k] + an->scale[k] * (p[k] + (q[k] - p[k]) * fr);
            nidx = fr < 0.5f ? p[3] : q[3];
        }
        if (lut && nidx < sc->nnormal) {
            int k;
            for (k = 0; k < 3; k++) {
                unsigned c = ((unsigned)v[j].c[k] * lut[nidx * 3 + k] + 128) >> 8;
                v[j].c[k] = (uint8_t)(c > 255 ? 255 : c);
            }
        }
    }
    if (w->surf[batch] >= 0)
        gsfx_surface(&sc->surf[w->surf[batch]], v, b->vcount, t, (uint32_t)batch);
}

/* A surface effect on n vertices at story time t (seed: the flicker's). */
void gsfx_surface(const sc_surf *s, rb_vertex *v, uint32_t n, double t, uint32_t seed)
{
    const float *p = s->p;
    uint32_t j;
    for (j = 0; j < n; j++) {
        rb_vertex *x = &v[j];
        float k = 1, u0 = x->u, v0 = x->v;
        switch (s->kind) {
        case SC_SK_SCROLL:
            x->u += (float)fmod(p[0] * t, 1.0);
            x->v += (float)fmod(p[1] * t, 1.0);
            continue;
        case SC_SK_WARP:
            x->u = u0 + p[0] * (float)sin(p[1] * v0 + p[2] * t);
            x->v = v0 + p[0] * (float)sin(p[1] * u0 + p[2] * t);
            continue;
        case SC_SK_RAMP: {
            float fade = p[0] > 0 ? p[0] : 1e-3f;
            k = (float)((t - x->u2) / fade);
            k = k < 0 ? 0 : k > 1 ? 1 : k;
            if (x->v2 > 0) {
                float off = (float)((x->v2 - t) / fade);
                k *= off < 0 ? 0 : off > 1 ? 1 : off;
            }
            x->c[3] = scale8(x->c[3], k);
            break;
        }
        default: {                          /* pulse */
            double ph = p[2] * t + x->u2;
            k = p[0] + p[1] * (p[3] != 0 ? flicker(seed, ph) : (float)sin(2 * VM_PI * ph));
            if (k < 0)
                k = 0;
            break;
        }
        }
        x->c[0] = scale8(x->c[0], k);
        x->c[1] = scale8(x->c[1], k);
        x->c[2] = scale8(x->c[2], k);
    }
}
