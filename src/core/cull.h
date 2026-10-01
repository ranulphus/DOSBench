/* cull.h - view-frustum tests (cull.c), shared by the level and the game scenes. */
#ifndef CULL_H
#define CULL_H
#include "vmath.h"

/* The six clip planes (a, b, c, d: inside when a x + b y + c z + d >= 0). */
typedef struct { float p[6][4]; } cull_frustum;

/* Planes of clip = projection * modelview, in the space the modelview takes in. */
void cull_planes(cull_frustum *f, const mat4 *clip);
/* 1 unless the box is wholly outside a plane. */
int cull_box(const cull_frustum *f, const float mins[3], const float maxs[3]);
/* 1 unless the sphere is wholly outside a plane (planes need not be unit length). */
int cull_sphere(const cull_frustum *f, const float c[3], float r);
/* Camera right and up in world space (the first two rows of a view matrix),
 * for camera-facing quads. */
void cull_view_axes(const mat4 *view, float right[3], float up[3]);

#endif
