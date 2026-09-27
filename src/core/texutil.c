/* texutil.c - see texutil.h. */
#include "texutil.h"

int tu_is_pow2(int v)
{
    return v > 0 && !(v & (v - 1));
}

int tu_levels(int w, int h)
{
    int n = 1;
    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        n++;
    }
    return n;
}

void tu_mip(const uint8_t *src, int w, int h, uint8_t *dst)
{
    int dw = w > 1 ? w / 2 : 1, dh = h > 1 ? h / 2 : 1;
    int sx = w > 1 ? 2 : 1, sy = h > 1 ? 2 : 1;
    int x, y, k;
    for (y = 0; y < dh; y++)
        for (x = 0; x < dw; x++) {
            const uint8_t *a = src + ((long)(y * sy) * w + x * sx) * 4;
            const uint8_t *b = a + (sx - 1) * 4;
            const uint8_t *c = a + (long)(sy - 1) * w * 4;
            const uint8_t *d = c + (sx - 1) * 4;
            for (k = 0; k < 4; k++)
                dst[((long)y * dw + x) * 4 + k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) >> 2);
        }
}

int tu_classify(const uint8_t *rgba, long n, int *grey)
{
    int cls = TU_OPAQUE;
    long i;
    *grey = 1;
    for (i = 0; i < n; i++, rgba += 4) {
        if (rgba[0] != rgba[1] || rgba[1] != rgba[2])
            *grey = 0;
        if (rgba[3] != 255)
            cls = rgba[3] == 0 && cls != TU_ALPHA ? TU_BINARY : TU_ALPHA;
    }
    return cls;
}
