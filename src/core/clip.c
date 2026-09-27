/* clip.c - Sutherland-Hodgman clipping in homogeneous clip space. */
#include "clip.h"
#include <string.h>

static float dist(const cl_vtx *v, int plane, float gbx, float gby)
{
    switch (plane) {
    case 0: return v->z + v->w;
    case 1: return v->w - v->z;
    case 2: return v->x + gbx * v->w;
    case 3: return gbx * v->w - v->x;
    case 4: return v->y + gby * v->w;
    default: return gby * v->w - v->y;
    }
}

unsigned cl_outcode(const cl_vtx *v, float gbx, float gby)
{
    unsigned oc = 0;
    int p;
    for (p = 0; p < 6; p++)
        if (dist(v, p, gbx, gby) < 0)
            oc |= 1u << p;
    return oc;
}

static void lerp(cl_vtx *o, const cl_vtx *a, const cl_vtx *b, float t)
{
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
    o->r = a->r + (b->r - a->r) * t;
    o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t;
    o->a = a->a + (b->a - a->a) * t;
    o->u = a->u + (b->u - a->u) * t;
    o->v = a->v + (b->v - a->v) * t;
}

int cl_polygon(const cl_vtx *in, int n, cl_vtx *out, unsigned mask, float gbx, float gby)
{
    cl_vtx buf[2][CL_MAX_OUT];
    const cl_vtx *src = in;
    int p, cur = 0, i;
    if (n > CL_MAX_OUT - 6)
        return 0;
    for (p = 0; p < 6 && n > 0; p++) {
        cl_vtx *dst = buf[cur];
        int m = 0;
        if (!(mask & (1u << p)))
            continue;
        for (i = 0; i < n; i++) {
            const cl_vtx *a = &src[i], *b = &src[(i + 1) % n];
            float da = dist(a, p, gbx, gby), db = dist(b, p, gbx, gby);
            if (da >= 0)
                dst[m++] = *a;
            if ((da >= 0) != (db >= 0))
                lerp(&dst[m++], a, b, da / (da - db));
        }
        src = dst;
        n = m;
        cur ^= 1;
    }
    if (n > 0)
        memcpy(out, src, (size_t)n * sizeof *out);
    return n < 3 ? 0 : n;
}
