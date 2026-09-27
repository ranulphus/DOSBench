/* clip.h - homogeneous polygon clipping for the Glide backend, which, as
 * Glide games did, transforms, clips and projects on the CPU. Triangles are
 * clipped against the near and far planes and against a guard band in x and
 * y (gbx, gby in NDC units, >= 1); the Glide clip window scissors the rest. */
#ifndef CLIP_H
#define CLIP_H

typedef struct {
    float x, y, z, w;           /* clip space */
    float r, g, b, a;           /* 0..255 */
    float u, v;
} cl_vtx;

enum { CL_NEAR = 1, CL_FAR = 2, CL_LEFT = 4, CL_RIGHT = 8, CL_BOTTOM = 16, CL_TOP = 32 };
#define CL_MAX_OUT 12           /* a triangle clipped by six planes */

unsigned cl_outcode(const cl_vtx *v, float gbx, float gby);
/* Clip the convex polygon in[0..n) against the planes in mask; writes up to
 * CL_MAX_OUT vertices to out and returns their number (0 if culled). */
int cl_polygon(const cl_vtx *in, int n, cl_vtx *out, unsigned mask, float gbx, float gby);

#endif
