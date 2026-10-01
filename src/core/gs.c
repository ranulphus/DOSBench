/* gs.c - the game-scene runtime: placement, camera, lighting, passes (gs.h). */
#include "gs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- Random numbers without state ------------------------------------ */
static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float gs_rand(uint32_t seed, uint32_t n, uint32_t channel)
{
    return (float)(hash32(seed * 0x9E3779B9u ^ hash32(n * 0x85EBCA6Bu + channel * 0xC2B2AE35u)) >> 8) *
           (1.0f / 16777216.0f);
}

/* ---- Tracks ------------------------------------------------------------ */
void gs_track_at(const sc_track *tr, double d, float pos[3], float fwd[3], float up[3])
{
    int n = (int)tr->nkeys, closed = tr->flags & 1, k, i0, i1, i2, i3;
    double spacing = tr->length / (closed ? n : n - 1), u;
    float t, t2, roll, left[3], y[3] = { 0, 1, 0 }, c, s;
    const float *K = tr->keys, *p0, *p1, *p2, *p3;
    if (closed) {
        u = fmod(d / spacing, (double)n);
        if (u < 0)
            u += n;
    } else {
        u = d / spacing;
        if (u < 0)
            u = 0;
        if (u > n - 1)
            u = n - 1;
    }
    k = (int)u;
    if (!closed && k > n - 2)
        k = n - 2;
    if (k > n - 1)
        k = n - 1;
    t = (float)(u - k);
    if (closed) {
        i0 = (k + n - 1) % n; i1 = k; i2 = (k + 1) % n; i3 = (k + 2) % n;
    } else {
        i0 = k ? k - 1 : 0; i1 = k; i2 = k + 1; i3 = k + 2 < n ? k + 2 : n - 1;
    }
    p0 = K + i0 * 4; p1 = K + i1 * 4; p2 = K + i2 * 4; p3 = K + i3 * 4;
    v3_catmull(pos, p0, p1, p2, p3, t);
    t2 = t * t;
    for (i0 = 0; i0 < 3; i0++)                 /* the derivative of the same curve */
        fwd[i0] = 0.5f * ((-p0[i0] + p2[i0]) + 2 * (2 * p0[i0] - 5 * p1[i0] + 4 * p2[i0] - p3[i0]) * t +
                          3 * (-p0[i0] + 3 * p1[i0] - 3 * p2[i0] + p3[i0]) * t2);
    if (v3_dot(fwd, fwd) < 1e-12f)
        v3_sub(fwd, p2, p1);
    v3_norm(fwd);
    /* Up: the world's, square to fwd, turned about fwd by the roll (+ leans left). */
    roll = (p1[3] + (p2[3] - p1[3]) * t) * (float)(VM_PI / 180.0);
    v3_cross(left, y, fwd);
    if (v3_dot(left, left) < 1e-12f) {          /* straight up or down */
        left[0] = 1; left[1] = 0; left[2] = 0;
    }
    v3_norm(left);
    v3_cross(y, fwd, left);
    c = (float)cos(roll);
    s = (float)sin(roll);
    up[0] = c * y[0] + s * left[0];
    up[1] = c * y[1] + s * left[1];
    up[2] = c * y[2] + s * left[2];
}

/* ---- Instances --------------------------------------------------------- */
static int alive(const scene *sc, const sc_inst *n, double t)
{
    double r = sc->ghdr->rate;
    return t >= n->f0 / r && (n->f1 == 0 || t < n->f1 / r);
}

/* Instance i's own placement (before its parent's) at story time t. */
static void inst_local(const scene *sc, const sc_inst *n, double t, mat4 *m)
{
    const float *p = n->p;
    double tau = t - n->f0 / sc->ghdr->rate;
    float pos[3], fwd[3], up[3], left[3], s;
    switch (n->kind) {
    case SC_IK_TRACK: {
        double d = p[0] + p[1] * tau + 0.5 * p[9] * tau * tau + p[5] * sin(2 * VM_PI * p[6] * tau + p[7]);
        gs_track_at(&sc->track[n->track], d, pos, fwd, up);
        v3_cross(left, up, fwd);
        v3_norm(left);
        pos[0] += left[0] * p[2] + up[0] * p[3];
        pos[1] += left[1] * p[2] + up[1] * p[3];
        pos[2] += left[2] * p[2] + up[2] * p[3];
        m4_from_frame(m, pos, fwd, up, p[4] != 0 ? p[4] : 1.0f);
        if (p[8] != 0)
            m4_rotate_y(m, p[8]);
        break;
    }
    case SC_IK_ORBIT: {
        double a = (p[5] + p[4] * tau) * VM_PI / 180.0;
        mat4 tilt;
        float q[3], q2[3], o[4], o2[4];
        s = p[7] != 0 ? p[7] : 1.0f;
        m4_identity(&tilt);
        m4_rotate_x(&tilt, p[6]);
        q[0] = p[3] * (float)sin(a); q[1] = 0; q[2] = p[3] * (float)cos(a);
        m4_xform(&tilt, q, o);
        pos[0] = p[0] + o[0]; pos[1] = p[1] + o[1]; pos[2] = p[2] + o[2];
        if (n->flags & SC_IF_FACE) {             /* along the way it goes */
            double a2 = a + (p[4] >= 0 ? 0.01 : -0.01);
            q2[0] = p[3] * (float)sin(a2); q2[1] = 0; q2[2] = p[3] * (float)cos(a2);
            m4_xform(&tilt, q2, o2);
            fwd[0] = o2[0] - o[0]; fwd[1] = o2[1] - o[1]; fwd[2] = o2[2] - o[2];
            up[0] = tilt.m[4]; up[1] = tilt.m[5]; up[2] = tilt.m[6];
            m4_from_frame(m, pos, fwd, up, s);
        } else {
            m4_identity(m);
            m4_translate(m, pos[0], pos[1], pos[2]);
            m4_scale(m, s, s, s);
        }
        if (p[8] != 0)
            m4_rotate_y(m, p[8]);
        break;
    }
    default:                                    /* static, spin */
        s = p[6] != 0 ? p[6] : 1.0f;
        m4_identity(m);
        m4_translate(m, p[0], p[1], p[2]);
        m4_ypr(m, p[3], p[4], p[5]);
        m4_scale(m, s, s, s);
        if (n->kind == SC_IK_SPIN)
            m4_rotate_axis(m, p + 7, (float)fmod(p[11] + p[10] * tau, 360.0));
        break;
    }
}

int gs_inst_at(const scene *sc, int i, double t, mat4 *m)
{
    const sc_inst *n = &sc->inst[i];
    if (!alive(sc, n, t))
        return 0;
    inst_local(sc, n, t, m);
    if (n->parent != SC_NONE) {
        mat4 pm;
        if (!gs_inst_at(sc, n->parent, t, &pm))
            return 0;
        m4_mul(m, &pm, m);
    }
    return 1;
}

/* ---- Camera ------------------------------------------------------------ */
static void col(const mat4 *m, int c, float v[3])
{
    v[0] = m->m[c * 4]; v[1] = m->m[c * 4 + 1]; v[2] = m->m[c * 4 + 2];
}

static void point(const mat4 *m, const float p[3], float out[3])
{
    float o[4];
    m4_xform(m, p, o);
    out[0] = o[0]; out[1] = o[1]; out[2] = o[2];
}

void gs_camera(const scene *sc, int f, float eye[3], float at[3], float up[3], float *fov)
{
    const sc_cam *c = sc->cam;
    const float *p;
    double tau, t = f / sc->ghdr->rate;
    mat4 m;
    float fwd[3], u[3], left[3], y[3] = { 0, 1, 0 };
    int i, have = 0, k;
    for (i = 0; i < sc->ncam; i++)
        if ((uint32_t)f >= sc->cam[i].f0 && (uint32_t)f < sc->cam[i].f1) {
            c = &sc->cam[i];
            break;
        }
    p = c->p;
    tau = (f - (double)c->f0) / sc->ghdr->rate;
    up[0] = 0; up[1] = 1; up[2] = 0;
    *fov = c->fov > 0 ? c->fov : sc->ghdr->fovy;
    if (c->target != SC_NONE)
        have = gs_inst_at(sc, c->target, t, &m);
    switch (c->kind) {
    case SC_CK_PATH:
        gs_track_at(&sc->track[c->track], p[0] + p[1] * tau, eye, fwd, u);
        eye[1] += p[2];
        if (have) {
            col(&m, 3, at);
        } else if (c->look_track != SC_NONE) {
            gs_track_at(&sc->track[c->look_track], p[3] + p[4] * tau, at, fwd, u);
            at[1] += p[5];
        } else {
            gs_track_at(&sc->track[c->track], p[0] + p[1] * tau + (p[3] != 0 ? p[3] : 10.0f), at, fwd, u);
            at[1] += p[2];
        }
        break;
    case SC_CK_CHASE:
        if (!have)
            m4_identity(&m);
        col(&m, 3, at);
        col(&m, 2, fwd);
        col(&m, 0, left);
        v3_norm(fwd);
        v3_norm(left);
        for (k = 0; k < 3; k++) {
            eye[k] = at[k] - fwd[k] * p[0] + y[k] * p[1] + left[k] * p[4];
            at[k] = at[k] + fwd[k] * p[2] + y[k] * p[3];
        }
        break;
    case SC_CK_MOUNT: {
        float q[3];
        if (!have)
            m4_identity(&m);
        point(&m, p, eye);
        q[0] = p[0] + p[3]; q[1] = p[1] + p[4]; q[2] = p[2] + p[5];
        point(&m, q, at);
        col(&m, 1, up);
        v3_norm(up);
        break;
    }
    case SC_CK_ORBIT: {
        float centre[3];
        double a = (p[6] + p[5] * tau) * VM_PI / 180.0;
        if (have)
            col(&m, 3, centre);
        else {
            centre[0] = p[0]; centre[1] = p[1]; centre[2] = p[2];
        }
        eye[0] = centre[0] + p[3] * (float)sin(a);
        eye[1] = centre[1] + p[4];
        eye[2] = centre[2] + p[3] * (float)cos(a);
        at[0] = centre[0]; at[1] = centre[1] + p[7]; at[2] = centre[2];
        break;
    }
    default:                                    /* fixed */
        eye[0] = p[0]; eye[1] = p[1]; eye[2] = p[2];
        if (have) {
            col(&m, 3, at);
            at[1] += p[3];
        } else {
            at[0] = p[4]; at[1] = p[5]; at[2] = p[6];
        }
        break;
    }
    v3_sub(fwd, at, eye);
    if (v3_dot(fwd, fwd) < 1e-6f)              /* never look at the eye itself */
        at[2] = eye[2] - 1;
}

/* ---- Set-up -------------------------------------------------------------- */
int gs_init(gs_world *w, scene *sc, const char **why)
{
    int i, k;
    long maxv = 1;
    memset(w, 0, sizeof *w);
    w->sc = sc;
    w->g = sc->ghdr;
    if (!w->g) {
        *why = "not-a-game-scene";
        return -1;
    }
    w->world = (mat4 *)calloc((size_t)sc->ninst + 1, sizeof *w->world);
    w->radius = (float *)calloc((size_t)sc->ninst + 1, sizeof *w->radius);
    w->drawn = (uint16_t *)calloc((size_t)sc->ninst + 1, sizeof *w->drawn);
    w->order = (int *)calloc((size_t)sc->ninst + 1, sizeof *w->order);
    w->depth = (float *)calloc((size_t)sc->ninst + 1, sizeof *w->depth);
    w->cpu = (uint8_t *)calloc((size_t)sc->nbatch + 1, 1);
    w->surf = (int16_t *)malloc(((size_t)sc->nbatch + 1) * sizeof *w->surf);
    if (!w->world || !w->radius || !w->drawn || !w->order || !w->depth || !w->cpu || !w->surf) {
        *why = "out-of-memory";
        return -1;
    }
    for (i = 0; i < sc->nbatch; i++)
        w->surf[i] = -1;
    for (i = 0; i < sc->nsurf; i++) {
        w->surf[sc->surf[i].batch] = (int16_t)i;
        w->cpu[sc->surf[i].batch] = 1;
    }
    for (i = 0; i < sc->nmodel; i++)
        if (sc->model[i].flags & (SC_MF_LIT | SC_MF_ANIM))
            for (k = sc->model[i].batch0; k < sc->model[i].batch0 + sc->model[i].nbatch; k++)
                w->cpu[k] = 1;
    for (i = 0; i < sc->nbatch; i++)
        if (w->cpu[i] && (long)sc->batch[i].vcount > maxv)
            maxv = (long)sc->batch[i].vcount;
    w->vbuf_n = maxv;
    w->vbuf = (rb_vertex *)malloc(sizeof *w->vbuf * (size_t)maxv);
    w->pmax = gsfx_capacity(sc);
    w->pt = (gs_particle *)malloc(sizeof *w->pt * (size_t)(w->pmax + 1));
    w->porder = (int *)malloc(sizeof *w->porder * (size_t)(w->pmax + 1));
    w->pv = (rb_vertex *)malloc(sizeof *w->pv * 4 * 4096);
    w->pi = (uint16_t *)malloc(sizeof *w->pi * 6 * 4096);
    if (!w->vbuf || !w->pt || !w->porder || !w->pv || !w->pi) {
        *why = "out-of-memory";
        return -1;
    }
    for (k = 0; k < 4096; k++) {               /* particle quads: two triangles each */
        uint16_t b = (uint16_t)(k * 4);
        uint16_t *q = w->pi + k * 6;
        q[0] = b; q[1] = (uint16_t)(b + 1); q[2] = (uint16_t)(b + 2);
        q[3] = b; q[4] = (uint16_t)(b + 2); q[5] = (uint16_t)(b + 3);
    }
    return 0;
}

void gs_free(gs_world *w)
{
    free(w->world); free(w->radius); free(w->drawn); free(w->order); free(w->depth);
    free(w->cpu); free(w->surf); free(w->vbuf); free(w->pt); free(w->porder); free(w->pv); free(w->pi);
    memset(w, 0, sizeof *w);
}

void gs_prefetch(gs_world *w)
{
    scene *sc = w->sc;
    mat4 p, mv;
    rb_state st;
    rb_vertex v[3];
    uint16_t idx[3] = { 0, 1, 2 };
    int i;
    memset(v, 0, sizeof v);
    v[1].x = 1; v[2].y = 1;
    db_projection(&p, DB_FOVY, DB_ZNEAR, DB_ZFAR);
    db_pixel_view(&mv, 10.0f);
    rb_set_matrices(&p, &mv);
    db_state_default(&st);
    st.tex = RB_TEX_MODULATE;
    rb_set_state(&st);
    for (i = 0; i < sc->ntex; i++) {
        rb_tex_bind(sc->rtex[i]);
        rb_draw(v, 3, idx, 3);
    }
    rb_tex_bind(NULL);
    rb_clear(0);
}

/* ---- Drawing --------------------------------------------------------------- */
void gs_set_state(gs_world *w, const rb_state *st)
{
    if (w->have_last && !memcmp(&w->last, st, sizeof *st))
        return;
    w->last = *st;
    w->have_last = 1;
    rb_set_state(st);
}

void gs_bind(gs_world *w, rb_tex *t)
{
    if (t == w->last_tex)
        return;
    w->last_tex = t;
    rb_tex_bind(t);
}

enum { P_OPAQUE, P_DECAL, P_BLEND, P_ADD };

static int pass_of(const sc_batch *b)
{
    if (b->flags & (SC_BF_ADD | SC_BF_GLOW))
        return P_ADD;
    if (b->flags & SC_BF_TRANS)
        return P_BLEND;
    if (b->flags & SC_BF_DECAL)
        return P_DECAL;
    return P_OPAQUE;
}

static void batch_state(const gs_world *w, const sc_batch *b, int sky, rb_state *st)
{
    db_state_default(st);
    st->bilinear = 1;
    st->tex = b->tex != SC_NONE ? RB_TEX_MODULATE : RB_TEX_OFF;
    st->cull = !(b->flags & SC_BF_TWOSIDED);
    st->alpha_test = (b->flags & SC_BF_ALPHATEST) != 0;
    if (b->flags & (SC_BF_ADD | SC_BF_GLOW)) {
        st->blend = RB_BLEND_ADD;
        st->depth_write = 0;
    } else if (b->flags & SC_BF_TRANS) {
        st->blend = RB_BLEND_ALPHA;
        st->depth_write = 0;
    }
    if (b->flags & SC_BF_DECAL)
        st->depth_write = 0;
    if ((w->g->flags & SC_GF_FOG) && !sky && !(b->flags & SC_BF_GLOW)) {
        st->fog = 1;
        st->fog_start = w->g->fog_start;
        st->fog_end = w->g->fog_end;
        st->fog_rgb = b->flags & SC_BF_ADD ? 0 : w->g->fog_rgb;   /* additive light fades out */
    }
    if (sky) {
        st->depth_test = 0;
        st->depth_write = 0;
    }
}

/* Vertex colour scale per normal (256 = 1) for a lit instance: ambient plus sun. */
static void light_lut(const gs_world *w, const mat4 *m, uint16_t *lut)
{
    const scene *sc = w->sc;
    float L[3];
    int n, c;
    m4_rot_inv(m, w->g->sun, L);
    v3_norm(L);
    for (n = 0; n < sc->nnormal; n++) {
        float d = v3_dot(sc->normal + n * 3, L);
        if (d < 0)
            d = 0;
        for (c = 0; c < 3; c++) {
            float v = (w->g->ambient[c] + w->g->sun_rgb[c] * d) * 256.0f;
            lut[n * 3 + c] = (uint16_t)(v < 0 ? 0 : v > 1023 ? 1023 : v + 0.5f);
        }
    }
}

/* Instance i's batches of one pass. */
static void draw_pass(gs_world *w, int i, int pass)
{
    scene *sc = w->sc;
    const sc_model *md = &sc->model[w->drawn[i]];
    uint16_t lut[256 * 3];
    int k, placed = 0, lit = 0;
    for (k = md->batch0; k < md->batch0 + md->nbatch; k++) {
        const sc_batch *b = &sc->batch[k];
        rb_state st;
        if (pass_of(b) != pass || !b->icount)
            continue;
        if (!placed) {
            mat4 mv;
            m4_mul(&mv, &w->view, &w->world[i]);
            rb_set_matrices(&w->proj, &mv);
            placed = 1;
        }
        if ((md->flags & SC_MF_LIT) && !lit) {
            light_lut(w, &w->world[i], lut);
            lit = 1;
        }
        batch_state(w, b, 0, &st);
        gs_set_state(w, &st);
        gs_bind(w, b->tex != SC_NONE ? sc->rtex[b->tex] : NULL);
        if (w->cpu[k]) {                        /* additive and glowing batches give light: never lit */
            gsfx_vertices(w, i, k, lit && pass != P_ADD ? lut : NULL);
            rb_draw(w->vbuf, (int)b->vcount, sc->idx + b->ifirst, (int)b->icount);
        } else
            rb_mesh_draw(sc->mesh[k]);
        w->tris += b->icount / 3;
    }
}

/* The sky model, around the camera: the view's rotation only, no depth, no fog. */
static void draw_sky(gs_world *w)
{
    scene *sc = w->sc;
    const sc_model *md = &sc->model[w->g->sky];
    mat4 mv = w->view;
    int k;
    mv.m[12] = mv.m[13] = mv.m[14] = 0;
    rb_set_matrices(&w->proj, &mv);
    for (k = md->batch0; k < md->batch0 + md->nbatch; k++) {
        const sc_batch *b = &sc->batch[k];
        rb_state st;
        batch_state(w, b, 1, &st);
        gs_set_state(w, &st);
        gs_bind(w, b->tex != SC_NONE ? sc->rtex[b->tex] : NULL);
        if (w->cpu[k]) {
            gsfx_vertices(w, -1, k, NULL);
            rb_draw(w->vbuf, (int)b->vcount, sc->idx + b->ifirst, (int)b->icount);
        } else
            rb_mesh_draw(sc->mesh[k]);
        w->tris += b->icount / 3;
    }
}

static int by_index(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static const float *sort_depth;
static int far_first(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (sort_depth[x] != sort_depth[y])
        return sort_depth[x] > sort_depth[y] ? -1 : 1;
    return x - y;
}

double gs_frame(gs_world *w, int f)
{
    scene *sc = w->sc;
    const sc_ghdr *g = w->g;
    mat4 clip;
    float view_dir[3];
    int i, nb = 0;
    w->t = f / g->rate;
    w->tris = 0;
    w->have_last = 0;
    w->last_tex = (rb_tex *)&w->last_tex;       /* nothing bound yet */
    gs_camera(sc, f, w->eye, w->at, w->up, &w->fov);
    db_projection(&w->proj, w->fov, g->znear, g->zfar);
    m4_lookat(&w->view, w->eye, w->at, w->up);
    m4_mul(&clip, &w->proj, &w->view);
    cull_planes(&w->fr, &clip);
    v3_sub(view_dir, w->at, w->eye);
    v3_norm(view_dir);
    rb_clear(g->flags & SC_GF_FOG ? g->fog_rgb : g->clear_rgb);
    /* Place the instances (parents come first), cull, choose the LOD. */
    for (i = 0; i < sc->ninst; i++) {
        const sc_inst *n = &sc->inst[i];
        const sc_model *md;
        float c[3], d[3], sx, sy, sz, s;
        uint16_t m;
        w->drawn[i] = SC_NONE;
        w->radius[i] = 0;
        if (!alive(sc, n, w->t) || (n->parent != SC_NONE && w->radius[n->parent] < 0))
            goto gone;
        inst_local(sc, n, w->t, &w->world[i]);
        if (n->parent != SC_NONE)
            m4_mul(&w->world[i], &w->world[n->parent], &w->world[i]);
        md = &sc->model[n->model];
        point(&w->world[i], md->centre, c);
        sx = v3_dot(w->world[i].m, w->world[i].m);
        sy = v3_dot(w->world[i].m + 4, w->world[i].m + 4);
        sz = v3_dot(w->world[i].m + 8, w->world[i].m + 8);
        s = (float)sqrt(sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz));
        w->radius[i] = md->radius * s;
        if (!(n->flags & SC_IF_NOCULL) && !cull_sphere(&w->fr, c, w->radius[i]))
            continue;                           /* placed (children may need it), not drawn */
        v3_sub(d, c, w->eye);
        m = n->model;
        {
            float dist = (float)sqrt(v3_dot(d, d));
            while (sc->model[m].lod_next != SC_NONE && sc->model[m].lod_dist > 0 && dist > sc->model[m].lod_dist)
                m = sc->model[m].lod_next;
        }
        w->drawn[i] = m;
        w->depth[i] = v3_dot(d, view_dir);
        w->order[nb++] = i;
        continue;
gone:
        w->radius[i] = -1;                      /* not there: neither are its children */
    }
    if (g->sky != SC_NONE)
        draw_sky(w);
    for (i = 0; i < nb; i++)
        draw_pass(w, w->order[i], P_OPAQUE);
    for (i = 0; i < nb; i++)
        draw_pass(w, w->order[i], P_DECAL);
    sort_depth = w->depth;
    qsort(w->order, (size_t)nb, sizeof *w->order, far_first);
    for (i = 0; i < nb; i++)
        draw_pass(w, w->order[i], P_BLEND);
    gsfx_particles(w);
    rb_set_matrices(&w->proj, &w->view);
    gsfx_draw_particles(w, 0);
    qsort(w->order, (size_t)nb, sizeof *w->order, by_index);    /* additive: in instance order */
    for (i = 0; i < nb; i++)
        draw_pass(w, w->order[i], P_ADD);
    rb_set_matrices(&w->proj, &w->view);
    gsfx_draw_particles(w, 1);
    rb_tex_bind(NULL);
    return w->tris;
}
