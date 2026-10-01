/* ov.c - text and boxes over the picture (ov.h).
 *
 * The font is the video BIOS's own 8x16 character set (INT 10h AX=1130h,
 * BH=6), read once at start-up and put in a 128x256 texture of 16 x 16
 * cells with on/off alpha, so it is drawn alpha-tested and point-sampled:
 * the same path on Glide (ARGB1555, which the G100's texel key handles) and
 * OpenGL. Nothing is copied from any font: each machine shows its own.
 * Without a BIOS (host builds) or if the call fails, glyphs are solid
 * blocks, so layout still shows. */
#include "ov.h"
#include "bench.h"
#include <stdlib.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(__DJGPP__)
#include "mga/sys.h"
#endif

#define TEX_W 128
#define TEX_H 256
#define EYE_D 10.0f
#define MAX_CH 160

static uint8_t font[256][OV_CH];
static int have_font;
static rb_tex *ftex;

void ov_load_font(void)
{
#if defined(__WATCOMC__) || defined(__DJGPP__)
    sys_rmregs r;
    const uint8_t *p;
    memset(&r, 0, sizeof r);
    r.eax = 0x1130;
    r.ebx = 0x0600;                     /* BH=6: the 8x16 font */
    if (sys_rm_int(0x10, &r) == 0 && (r.es || (r.ebp & 0xFFFF))) {
        p = (const uint8_t *)sys_real_ptr(r.es, (uint16_t)r.ebp);
        if (p) {
            memcpy(font, p, sizeof font);
            have_font = 1;
            return;
        }
    }
#endif
    {
        int c, y;                       /* fallback: a block per printable character */
        for (c = 33; c < 127; c++)
            for (y = 3; y < 13; y++)
                font[c][y] = 0x7E;
    }
}

int ov_open(void)
{
    uint8_t *img = (uint8_t *)calloc(TEX_W * TEX_H, 4);
    int c, x, y;
    if (!img)
        return -1;
    for (c = 0; c < 256; c++)
        for (y = 0; y < OV_CH; y++)
            for (x = 0; x < OV_CW; x++) {
                uint8_t *p = img + 4 * ((size_t)((c / 16) * OV_CH + y) * TEX_W + (c % 16) * OV_CW + x);
                p[0] = p[1] = p[2] = 255;
                p[3] = (uint8_t)((font[c][y] >> (7 - x)) & 1 ? 255 : 0);
            }
    ftex = rb_tex_create(TEX_W, TEX_H, img, RB_TF_CLAMP);
    free(img);
    return ftex ? 0 : -1;
}

void ov_close(void)
{
    rb_tex_free(ftex);
    ftex = NULL;
}

void ov_begin(void)
{
    mat4 p, mv;
    db_projection(&p, DB_FOVY, DB_ZNEAR, DB_ZFAR);
    db_pixel_view(&mv, EYE_D);
    rb_set_matrices(&p, &mv);
}

static void vtx(rb_vertex *v, float x, float y, uint32_t rgba, float u, float t)
{
    v->x = x; v->y = y; v->z = 0;
    v->c[0] = (uint8_t)(rgba >> 24); v->c[1] = (uint8_t)(rgba >> 16);
    v->c[2] = (uint8_t)(rgba >> 8); v->c[3] = (uint8_t)rgba;
    v->u = v->u2 = u;
    v->v = v->v2 = t;
}

/* Quad (x0,y0)-(x1,y1) as two counter-clockwise triangles (pixel space, y down). */
static void quad(rb_vertex *v, uint16_t *ix, int base, float x0, float y0, float x1, float y1, uint32_t rgba,
                 float u0, float t0, float u1, float t1)
{
    vtx(&v[0], x0, y0, rgba, u0, t0);
    vtx(&v[1], x1, y0, rgba, u1, t0);
    vtx(&v[2], x1, y1, rgba, u1, t1);
    vtx(&v[3], x0, y1, rgba, u0, t1);
    ix[0] = (uint16_t)base; ix[1] = (uint16_t)(base + 2); ix[2] = (uint16_t)(base + 1);
    ix[3] = (uint16_t)base; ix[4] = (uint16_t)(base + 3); ix[5] = (uint16_t)(base + 2);
}

void ov_box(int x0, int y0, int x1, int y1, uint32_t rgba)
{
    rb_vertex v[4];
    uint16_t ix[6];
    rb_state st;
    memset(&st, 0, sizeof st);
    st.blend = (rgba & 0xFF) < 255 ? RB_BLEND_ALPHA : RB_BLEND_NONE;
    rb_set_state(&st);
    rb_tex_bind(NULL);
    quad(v, ix, 0, (float)x0, (float)y0, (float)x1, (float)y1, rgba, 0, 0, 0, 0);
    rb_draw(v, 4, ix, 6);
}

int ov_text_width(const char *s, int scale)
{
    return (int)strlen(s) * OV_CW * scale;
}

int ov_text(int x, int y, int scale, uint32_t rgba, const char *s)
{
    static rb_vertex v[MAX_CH * 4];
    static uint16_t ix[MAX_CH * 6];
    rb_state st;
    int n = 0, cx = x;
    if (!ftex)
        return ov_text_width(s, scale);
    for (; *s && n < MAX_CH; s++, cx += OV_CW * scale) {
        unsigned c = (unsigned char)*s;
        float u0 = (float)(c % 16) * OV_CW / TEX_W, t0 = (float)(c / 16) * OV_CH / TEX_H;
        if (c == ' ')
            continue;
        quad(&v[n * 4], &ix[n * 6], n * 4, (float)cx, (float)y, (float)(cx + OV_CW * scale),
             (float)(y + OV_CH * scale), rgba, u0, t0, u0 + (float)OV_CW / TEX_W, t0 + (float)OV_CH / TEX_H);
        n++;
    }
    if (n) {
        memset(&st, 0, sizeof st);
        st.alpha_test = 1;
        st.tex = RB_TEX_MODULATE;
        rb_set_state(&st);
        rb_tex_bind(ftex);
        rb_draw(v, n * 4, ix, n * 6);
        rb_tex_bind(NULL);
    }
    return cx - x;
}

int ov_wrap(int x, int y, int w, int scale, uint32_t rgba, const char *s)
{
    char line[MAX_CH + 1];
    int per = w / (OV_CW * scale), lines = 0;
    if (per < 8)
        per = 8;
    if (per > MAX_CH)
        per = MAX_CH;
    while (*s) {
        int n = (int)strlen(s), cut = n;
        if (n > per) {                  /* break at the last space that fits */
            cut = per;
            while (cut > 0 && s[cut] != ' ')
                cut--;
            if (cut == 0)
                cut = per;
        }
        memcpy(line, s, (size_t)cut);
        line[cut] = 0;
        ov_text(x, y + lines * OV_CH * scale * 5 / 4, scale, rgba, line);
        lines++;
        s += cut;
        while (*s == ' ')
            s++;
    }
    return lines;
}
