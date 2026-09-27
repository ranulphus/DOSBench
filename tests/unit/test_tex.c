/* test_tex.c - mip generation, alpha classes and Glide texel formats. */
#include "unit.h"
#include "texutil.h"
#include "gtex.h"
#include "glide/glide2.h"
#include <string.h>

void unit_run(void)
{
    uint8_t img[4 * 2 * 4], mip[2 * 1 * 4];
    uint16_t out16[8];
    uint8_t out8[8];
    int grey, i;
    float ss, ts;
    CHECK(tu_levels(256, 256) == 9);
    CHECK(tu_levels(256, 32) == 9);
    CHECK(tu_levels(1, 1) == 1);
    CHECK(tu_is_pow2(64) && !tu_is_pow2(48) && !tu_is_pow2(0));
    /* 4x2 -> 2x1 box filter. */
    for (i = 0; i < 8; i++) {
        img[i * 4 + 0] = (uint8_t)(i * 10); img[i * 4 + 1] = 100; img[i * 4 + 2] = 0; img[i * 4 + 3] = 255;
    }
    tu_mip(img, 4, 2, mip);
    CHECK(mip[0] == (0 + 10 + 40 + 50 + 2) / 4);
    CHECK(mip[4] == (20 + 30 + 60 + 70 + 2) / 4);
    CHECK(mip[1] == 100 && mip[7] == 255);
    /* Classes. */
    CHECK(tu_classify(img, 8, &grey) == TU_OPAQUE && !grey);
    img[3] = 0;
    CHECK(tu_classify(img, 8, &grey) == TU_BINARY);
    img[7] = 128;
    CHECK(tu_classify(img, 8, &grey) == TU_ALPHA);
    memset(img, 77, sizeof img);
    for (i = 0; i < 8; i++) img[i * 4 + 3] = 255;
    CHECK(tu_classify(img, 8, &grey) == TU_OPAQUE && grey);
    /* Glide codes. */
    CHECK(gt_lod(256) == GR_LOD_256 && gt_lod(1) == GR_LOD_1 && gt_lod(48) == -1);
    CHECK(gt_aspect(256, 32) == GR_ASPECT_8x1 && gt_aspect(32, 64) == GR_ASPECT_1x2);
    CHECK(gt_aspect(64, 64) == GR_ASPECT_1x1 && gt_aspect(256, 16) == -1);
    gt_st_scale(128, 32, &ss, &ts);
    CHECK(ss == 256 && ts == 64);
    gt_st_scale(32, 64, &ss, &ts);
    CHECK(ss == 128 && ts == 256);
    CHECK(gt_format(TU_OPAQUE, 0) == GR_TEXFMT_RGB_565);
    CHECK(gt_format(TU_OPAQUE, 1) == GR_TEXFMT_INTENSITY_8);
    CHECK(gt_format(TU_BINARY, 0) == GR_TEXFMT_ARGB_1555);
    CHECK(gt_format(TU_ALPHA, 1) == GR_TEXFMT_ARGB_4444);
    /* Texel packing. */
    img[0] = 255; img[1] = 0; img[2] = 255; img[3] = 0;
    CHECK(gt_convert(img, 1, GR_TEXFMT_RGB_565, out16) == 2 && out16[0] == 0xF81F);
    gt_convert(img, 1, GR_TEXFMT_ARGB_1555, out16);
    CHECK(out16[0] == 0x7C1F);
    img[3] = 255;
    gt_convert(img, 1, GR_TEXFMT_ARGB_1555, out16);
    CHECK(out16[0] == 0xFC1F);
    img[3] = 0x80;
    gt_convert(img, 1, GR_TEXFMT_ARGB_4444, out16);
    CHECK(out16[0] == 0x8F0F);
    CHECK(gt_convert(img, 1, GR_TEXFMT_INTENSITY_8, out8) == 1 && out8[0] == 255);
    CHECK(gt_texel_bytes(GR_TEXFMT_INTENSITY_8) == 1 && gt_texel_bytes(GR_TEXFMT_RGB_565) == 2);
}
