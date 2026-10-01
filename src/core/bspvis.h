/* bspvis.h - Quake-style visibility over a .DBS VISL section (bspvis.c;
 * tools/bsp.py writes it): find the camera's leaf, decompress its row of
 * the potentially visible set, mark the faces of the visible leaves inside
 * the frustum, and gather the marked faces' indices per batch. */
#ifndef BSPVIS_H
#define BSPVIS_H
#include <stdint.h>
#include "cull.h"
#include "scene.h"

typedef struct { float n[3], d; int32_t child[2]; } bsp_node;
typedef struct { int32_t contents, visofs; float mins[3], maxs[3]; uint32_t firstmark, nmarks; } bsp_leaf;
typedef struct { uint16_t batch, icount; uint32_t ifirst; } bsp_face;

typedef struct {
    const bsp_node *nodes;
    const bsp_leaf *leaves;
    const uint32_t *marks;
    const bsp_face *faces;
    const uint8_t *visdata;
    uint32_t nnodes, nleaves, nmarks, nfaces, visbytes, visleafs, headnode;
    uint32_t *facevis, visframe;
    uint8_t *pvs;
    long pvs_leaf;                      /* the leaf pvs holds (a cache: output never depends on it) */
    uint32_t *face0, *nface;            /* per batch: its faces (faces are sorted by batch) */
    uint32_t *count;                    /* per batch: indices gathered this frame */
    uint16_t *scratch;                  /* gathered indices, at each batch's ifirst */
} bsp_vis;

/* Parse sc's VISL section and allocate the per-frame arrays. 0, or -1 with
 * why ("no-visibility-data", "bad-face-batch", "bad-mark", "out-of-memory"). */
int  bsp_init(bsp_vis *b, const scene *sc, const char **why);
void bsp_free(bsp_vis *b);
long bsp_point_leaf(const bsp_vis *b, const float p[3]);
/* Make pvs the row of leaf. */
void bsp_pvs(bsp_vis *b, long leaf);
/* Whether leaf (1-based, as in the BSP) is in the decompressed row. */
int  bsp_leaf_visible(const bsp_vis *b, uint32_t leaf);
/* Mark and gather the faces seen from eye; returns the triangles gathered. */
double bsp_gather(bsp_vis *b, const scene *sc, const float eye[3], const cull_frustum *f);

#endif
