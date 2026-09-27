/* gtex.h - Glide 2.x texture helpers: LOD and aspect codes, s/t scale, and
 * RGBA8 to Voodoo texel formats. Pure C, unit-tested on the host. */
#ifndef GTEX_H
#define GTEX_H
#include <stddef.h>
#include <stdint.h>

int    gt_lod(int size);                 /* GR_LOD_256..GR_LOD_1, or -1 */
int    gt_aspect(int w, int h);          /* GR_ASPECT_8x1..1x8, or -1 */
/* Glide s,t span 0..256 along the long side at every LOD. */
void   gt_st_scale(int w, int h, float *sscale, float *tscale);
/* Texel format for the given alpha class and greyness (texutil.h). */
int    gt_format(int alpha_class, int grey);
int    gt_texel_bytes(int format);
/* Convert one level; returns the bytes written. */
size_t gt_convert(const uint8_t *rgba, long n, int format, void *out);

#endif
