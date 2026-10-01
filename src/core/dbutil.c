/* dbutil.c - helpers the tests share (bench.h): default state, the
 * projection, the pixel view, procedural textures, data paths. Apart from
 * the programs' main loop so host tools (tests/host/screplay.c) can link
 * the tests without it. */
#include "bench.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__WATCOMC__) || defined(__DJGPP__)
#  define DB_SEP "\\"
#else
#  define DB_SEP "/"
#endif

void db_state_default(rb_state *s)
{
    memset(s, 0, sizeof *s);
    s->depth_test = 1;
    s->depth_write = 1;
}

void db_projection(mat4 *p, float fovy, float znear, float zfar)
{
    m4_perspective(p, fovy, (float)db.w / db.h, znear, zfar);
}

void db_pixel_view(mat4 *mv, float d)
{
    float s = 2.0f * d * (float)tan(DB_FOVY * VM_PI / 360.0) / db.h;
    m4_identity(mv);
    mv->m[0] = s;
    mv->m[5] = -s;
    mv->m[12] = -0.5f * db.w * s;
    mv->m[13] = 0.5f * db.h * s;
    mv->m[14] = -d;
}

void db_texture(uint8_t *p, int w, int h, int kind, int seed)
{
    int x, y;
    unsigned r0 = 60 + (seed * 71) % 150, g0 = 60 + (seed * 37) % 150, b0 = 60 + (seed * 113) % 150;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++, p += 4) {
            int on = ((x * 8 / w) + (y * 8 / h)) & 1;
            unsigned n = (unsigned)((x * 7919 + y * 104729 + seed * 31) % 23);
            switch (kind) {
            case 3: {                              /* grey, lightmap-like */
                unsigned v = 96 + (unsigned)(x * 100 / w) + n;
                p[0] = p[1] = p[2] = (uint8_t)(v > 255 ? 255 : v);
                p[3] = 255;
                break;
            }
            default:
                p[0] = (uint8_t)(on ? 230 : r0 + x * 60 / w);
                p[1] = (uint8_t)(on ? 220 - n : g0 + y * 60 / h);
                p[2] = (uint8_t)(on ? 90 + n : b0);
                if (kind == 1) {                   /* cut-out: a disc per cell */
                    int cx = (x % (w / 4)) - w / 8, cy = (y % (h / 4)) - h / 8;
                    p[3] = (uint8_t)(cx * cx + cy * cy <= (w / 10) * (w / 10) ? 255 : 0);
                } else if (kind == 2) {
                    p[3] = (uint8_t)(x * 255 / (w - 1));
                } else {
                    p[3] = 255;
                }
            }
        }
}

char *db_path(const char *file)
{
    static char buf[160];
    snprintf(buf, sizeof buf, "%s" DB_SEP "%s", db.data, file);
    return buf;
}
