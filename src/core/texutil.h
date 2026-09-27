/* texutil.h - RGBA8 texture helpers shared by both backends. */
#ifndef TEXUTIL_H
#define TEXUTIL_H
#include <stdint.h>

enum { TU_OPAQUE, TU_BINARY, TU_ALPHA };

int  tu_is_pow2(int v);
/* Levels in a full chain down to 1x1. */
int  tu_levels(int w, int h);
/* Box-filter one level: dst is max(1,w/2) x max(1,h/2). */
void tu_mip(const uint8_t *src, int w, int h, uint8_t *dst);
/* Alpha class of n texels, and whether every texel is grey (r == g == b). */
int  tu_classify(const uint8_t *rgba, long n, int *grey);

#endif
