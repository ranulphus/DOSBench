/* gtex.c - see gtex.h. */
#include "gtex.h"
#include "texutil.h"
#include "glide/glide2.h"

int gt_lod(int size)
{
    int lod;
    for (lod = GR_LOD_256; lod <= GR_LOD_1; lod++)
        if ((256 >> (lod - GR_LOD_256)) == size)
            return lod;
    return -1;
}

int gt_aspect(int w, int h)
{
    switch (w >= h ? w / h : -(h / w)) {
    case 8: return GR_ASPECT_8x1;
    case 4: return GR_ASPECT_4x1;
    case 2: return GR_ASPECT_2x1;
    case 1: case -1: return GR_ASPECT_1x1;
    case -2: return GR_ASPECT_1x2;
    case -4: return GR_ASPECT_1x4;
    case -8: return GR_ASPECT_1x8;
    default: return -1;
    }
}

void gt_st_scale(int w, int h, float *ss, float *ts)
{
    if (w >= h) {
        *ss = 256.0f;
        *ts = 256.0f * h / w;
    } else {
        *ss = 256.0f * w / h;
        *ts = 256.0f;
    }
}

int gt_format(int alpha_class, int grey)
{
    if (alpha_class == TU_OPAQUE)
        return grey ? GR_TEXFMT_INTENSITY_8 : GR_TEXFMT_RGB_565;
    return alpha_class == TU_BINARY ? GR_TEXFMT_ARGB_1555 : GR_TEXFMT_ARGB_4444;
}

int gt_texel_bytes(int format)
{
    return format < GR_TEXFMT_16BIT ? 1 : 2;
}

size_t gt_convert(const uint8_t *p, long n, int format, void *out)
{
    long i;
    if (format == GR_TEXFMT_INTENSITY_8) {
        uint8_t *o = (uint8_t *)out;
        for (i = 0; i < n; i++, p += 4)
            o[i] = p[0];
        return (size_t)n;
    }
    {
        uint16_t *o = (uint16_t *)out;
        for (i = 0; i < n; i++, p += 4) {
            switch (format) {
            case GR_TEXFMT_RGB_565:
                o[i] = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
                break;
            case GR_TEXFMT_ARGB_1555:
                o[i] = (uint16_t)((p[3] >= 128 ? 0x8000 : 0) | ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3));
                break;
            default:
                o[i] = (uint16_t)(((p[3] >> 4) << 12) | ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4));
                break;
            }
        }
        return (size_t)n * 2;
    }
}
