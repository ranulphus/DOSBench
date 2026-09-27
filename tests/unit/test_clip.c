/* test_clip.c - homogeneous clipping for the Glide backend. */
#include "unit.h"
#include "clip.h"
#include <string.h>

static cl_vtx v(float x, float y, float z, float w, float r)
{
    cl_vtx o;
    memset(&o, 0, sizeof o);
    o.x = x; o.y = y; o.z = z; o.w = w; o.r = r;
    return o;
}

void unit_run(void)
{
    cl_vtx in[3], out[CL_MAX_OUT];
    int n, i;
    in[0] = v(0, 0, 0, 1, 0); in[1] = v(0.5f, 0, 0, 1, 1); in[2] = v(0, 0.5f, 0, 1, 0.5f);
    CHECK(cl_outcode(&in[0], 2, 2) == 0);
    n = cl_polygon(in, 3, out, 0, 2, 2);
    CHECK(n == 3 && !memcmp(out, in, sizeof in));
    /* Near plane: a quad remains, every vertex on or in front of z = -w. */
    in[0] = v(0, 0, -2, 1, 0); in[1] = v(0.5f, 0, 0.5f, 1, 1); in[2] = v(0, 0.5f, 0.5f, 1, 1);
    CHECK(cl_outcode(&in[0], 2, 2) == CL_NEAR);
    n = cl_polygon(in, 3, out, CL_NEAR, 2, 2);
    CHECK(n == 4);
    for (i = 0; i < n; i++)
        CHECK(out[i].z + out[i].w >= -1e-5f);
    CHECK_NEAR(out[n - 1].r, 0.4, 1e-5);    /* 1 -> 0 over d = 1.5 -> -1 */
    /* Guard band in x: nothing beyond gbx * w survives. */
    in[0] = v(-10, 0, 0, 1, 0); in[1] = v(10, 0.2f, 0, 1, 0); in[2] = v(0, 1, 0, 1, 0);
    CHECK(cl_outcode(&in[0], 3, 3) == CL_LEFT && cl_outcode(&in[1], 3, 3) == CL_RIGHT);
    n = cl_polygon(in, 3, out, CL_LEFT | CL_RIGHT, 3, 3);
    CHECK(n >= 4);
    for (i = 0; i < n; i++)
        CHECK(out[i].x >= -3 - 1e-4f && out[i].x <= 3 + 1e-4f);
    /* Wholly outside one plane: nothing. */
    in[0] = v(0, 0, 5, 1, 0); in[1] = v(1, 0, 5, 1, 0); in[2] = v(0, 1, 5, 1, 0);
    CHECK(cl_polygon(in, 3, out, CL_FAR, 3, 3) == 0);
    /* Every plane at once stays within CL_MAX_OUT. */
    in[0] = v(-100, -100, -5, 1, 0); in[1] = v(100, -90, 5, 1, 0); in[2] = v(0, 100, 0.2f, 1, 0);
    n = cl_polygon(in, 3, out, 63, 1, 1);
    CHECK(n <= CL_MAX_OUT);
}
