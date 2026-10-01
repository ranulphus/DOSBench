/* ov.h - text and boxes over the picture: the title cards, captions and
 * summaries present.c draws between measurements (never in timed frames or
 * saved shots). Coordinates are pixels, x right, y down. */
#ifndef OV_H
#define OV_H
#include <stdint.h>

#define OV_CW 8                         /* glyph cell, at scale 1 */
#define OV_CH 16

void ov_load_font(void);                /* once, before the first rb_open: the video BIOS's 8x16 font */
int  ov_open(void);                     /* after rb_open: the font texture; 0 ok */
void ov_close(void);                    /* before rb_close */
void ov_begin(void);                    /* pixel-space matrices and overlay state */
/* Text in colour rgba (0xRRGGBBAA); returns its width in pixels. */
int  ov_text(int x, int y, int scale, uint32_t rgba, const char *s);
int  ov_text_width(const char *s, int scale);
/* Text wrapped at word boundaries to width w; returns the lines used. */
int  ov_wrap(int x, int y, int w, int scale, uint32_t rgba, const char *s);
/* A filled box; alpha below 255 blends over the picture. */
void ov_box(int x0, int y0, int x1, int y1, uint32_t rgba);

#endif
