/* bspvis.c - Quake-style visibility over a VISL section (bspvis.h). */
#include "bspvis.h"
#include <stdlib.h>
#include <string.h>

static int parse_vis(bsp_vis *b, const uint8_t *p, long size)
{
    long need;
    uint32_t h[8];
    if (!p || size < 32)
        return -1;
    memcpy(h, p, sizeof h);
    b->nnodes = h[0]; b->nleaves = h[1]; b->nmarks = h[2]; b->nfaces = h[3];
    b->visbytes = h[4]; b->visleafs = h[5]; b->headnode = h[6];
    need = 32 + (long)b->nnodes * 24 + (long)b->nleaves * 40 + (long)b->nmarks * 4 + (long)b->nfaces * 8 + b->visbytes;
    if (need > size)
        return -1;
    b->nodes = (const bsp_node *)(p + 32);
    b->leaves = (const bsp_leaf *)(p + 32 + b->nnodes * 24);
    b->marks = (const uint32_t *)((const uint8_t *)b->leaves + b->nleaves * 40);
    b->faces = (const bsp_face *)((const uint8_t *)b->marks + b->nmarks * 4);
    b->visdata = (const uint8_t *)b->faces + b->nfaces * 8;
    return 0;
}

int bsp_init(bsp_vis *b, const scene *sc, const char **why)
{
    uint32_t i;
    memset(b, 0, sizeof *b);
    if (parse_vis(b, sc->vis, sc->vis_size) < 0) {
        *why = "no-visibility-data";
        return -1;
    }
    b->facevis = (uint32_t *)calloc(b->nfaces + 1, sizeof *b->facevis);
    b->pvs = (uint8_t *)malloc((b->visleafs + 7) / 8 + 1);
    b->face0 = (uint32_t *)calloc((size_t)sc->nbatch + 1, sizeof *b->face0);
    b->nface = (uint32_t *)calloc((size_t)sc->nbatch + 1, sizeof *b->nface);
    b->count = (uint32_t *)calloc((size_t)sc->nbatch + 1, sizeof *b->count);
    b->scratch = (uint16_t *)malloc((size_t)(sc->nidx + 1) * sizeof *b->scratch);
    if (!b->facevis || !b->pvs || !b->face0 || !b->nface || !b->count || !b->scratch) {
        *why = "out-of-memory";
        return -1;
    }
    for (i = 0; i < b->nfaces; i++) {
        uint16_t bt = b->faces[i].batch;
        if (bt >= sc->nbatch) {
            *why = "bad-face-batch";
            return -1;
        }
        if (!b->nface[bt])
            b->face0[bt] = i;
        b->nface[bt]++;
    }
    for (i = 0; i < b->nmarks; i++)
        if (b->marks[i] >= b->nfaces) {
            *why = "bad-mark";
            return -1;
        }
    b->pvs_leaf = -1;
    return 0;
}

void bsp_free(bsp_vis *b)
{
    free(b->facevis); free(b->pvs); free(b->face0); free(b->nface); free(b->count); free(b->scratch);
    memset(b, 0, sizeof *b);
}

long bsp_point_leaf(const bsp_vis *b, const float p[3])
{
    int32_t n = (int32_t)b->headnode;
    int guard = 0;
    while (n >= 0 && (uint32_t)n < b->nnodes && guard++ < 4096) {
        const bsp_node *nd = &b->nodes[n];
        n = nd->child[nd->n[0] * p[0] + nd->n[1] * p[1] + nd->n[2] * p[2] - nd->d >= 0 ? 0 : 1];
    }
    n = -1 - n;
    return n >= 0 && (uint32_t)n < b->nleaves ? n : 0;
}

/* Quake's run-length coding: a zero byte is followed by a count of zero bytes. */
void bsp_pvs(bsp_vis *b, long leaf)
{
    uint32_t row = (b->visleafs + 7) / 8, o = 0;
    int32_t ofs = b->leaves[leaf].visofs;
    const uint8_t *in = b->visdata + (ofs > 0 ? ofs : 0), *end = b->visdata + b->visbytes;
    if (leaf == b->pvs_leaf)
        return;
    b->pvs_leaf = leaf;
    if (leaf == 0 || ofs < 0 || (uint32_t)ofs >= b->visbytes) {
        memset(b->pvs, 0xFF, row);           /* outside the map, or no data: everything */
        return;
    }
    while (o < row && in < end) {
        if (*in) {
            b->pvs[o++] = *in++;
            continue;
        }
        if (in + 1 >= end)
            break;
        {
            uint32_t c = in[1];
            in += 2;
            while (c-- && o < row)
                b->pvs[o++] = 0;
        }
    }
    while (o < row)
        b->pvs[o++] = 0;
}

int bsp_leaf_visible(const bsp_vis *b, uint32_t leaf)
{
    return leaf != 0 && leaf <= b->visleafs && (b->pvs[(leaf - 1) >> 3] & (1u << ((leaf - 1) & 7)));
}

double bsp_gather(bsp_vis *b, const scene *sc, const float eye[3], const cull_frustum *f)
{
    uint32_t l, bt, k;
    double tris = 0;
    bsp_pvs(b, bsp_point_leaf(b, eye));
    if (++b->visframe == 0)
        b->visframe = 1;
    for (l = 1; l < b->nleaves && l <= b->visleafs; l++) {
        const bsp_leaf *lf = &b->leaves[l];
        if (!(b->pvs[(l - 1) >> 3] & (1u << ((l - 1) & 7))) || !cull_box(f, lf->mins, lf->maxs))
            continue;
        for (k = 0; k < lf->nmarks && lf->firstmark + k < b->nmarks; k++)
            b->facevis[b->marks[lf->firstmark + k]] = b->visframe;
    }
    for (bt = 0; bt < (uint32_t)sc->nbatch; bt++) {
        const sc_batch *s = &sc->batch[bt];
        uint16_t *out = b->scratch + s->ifirst;
        uint32_t n = 0, fc;
        for (fc = b->face0[bt]; fc < b->face0[bt] + b->nface[bt]; fc++)
            if (b->facevis[fc] == b->visframe) {
                const bsp_face *face = &b->faces[fc];
                memcpy(out + n, sc->idx + face->ifirst, (size_t)face->icount * sizeof *out);
                n += face->icount;
            }
        b->count[bt] = n;
        tris += n / 3;
    }
    return tris;
}
