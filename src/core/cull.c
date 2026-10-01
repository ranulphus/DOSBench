/* cull.c - view-frustum tests (cull.h). */
#include "cull.h"
#include <math.h>

void cull_planes(cull_frustum *f, const mat4 *clip)
{
    const float *m = clip->m;
    int i, k;
    for (i = 0; i < 3; i++)
        for (k = 0; k < 4; k++) {
            f->p[i * 2][k] = m[k * 4 + 3] + m[k * 4 + i];
            f->p[i * 2 + 1][k] = m[k * 4 + 3] - m[k * 4 + i];
        }
}

int cull_box(const cull_frustum *f, const float mins[3], const float maxs[3])
{
    int i;
    for (i = 0; i < 6; i++) {
        const float *p = f->p[i];
        float x = p[0] >= 0 ? maxs[0] : mins[0], y = p[1] >= 0 ? maxs[1] : mins[1], z = p[2] >= 0 ? maxs[2] : mins[2];
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0)
            return 0;
    }
    return 1;
}

int cull_sphere(const cull_frustum *f, const float c[3], float r)
{
    int i;
    for (i = 0; i < 6; i++) {
        const float *p = f->p[i];
        float len = (float)sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (p[0] * c[0] + p[1] * c[1] + p[2] * c[2] + p[3] < -r * len)
            return 0;
    }
    return 1;
}

void cull_view_axes(const mat4 *view, float right[3], float up[3])
{
    right[0] = view->m[0]; right[1] = view->m[4]; right[2] = view->m[8];
    up[0] = view->m[1]; up[1] = view->m[5]; up[2] = view->m[9];
}
