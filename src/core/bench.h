/* bench.h - the benchmark core shared by BENCHG.EXE and BENCHGL.EXE. */
#ifndef BENCH_H
#define BENCH_H
#include <stdint.h>
#include "rb.h"
#include "vmath.h"

#define DB_VERSION "0.1"
#ifndef DB_BUILD_ID
#  define DB_BUILD_ID "unknown"
#endif

typedef struct {
    int quick;                  /* --quick: a few frames per test (Loop A) */
    int shots;                  /* --shots: save one frame per test */
    int vsync;
    int submit;                 /* RB_SUBMIT_* for static geometry */
    double secs;                /* target seconds per test */
    int warm, min_frames, max_frames;
    const char *data;           /* scene files */
    const char *out;
    char tag;                   /* image-name prefix: G, L, or --tag */
    int w, h;                   /* current mode */
} db_opts;

extern db_opts db;

/* Per-test context. setup() fills the per-frame work so the runner can
 * turn times into rates. */
typedef struct tctx {
    const struct test_def *def;
    double tris;                /* triangles submitted per frame */
    double pixels;              /* pixels written per frame (fill tests) */
    double texels;              /* texels uploaded per frame (upload tests) */
    double tris_drawn;          /* triangles drawn, added up by frame() when they vary */
    int frames;                 /* fixed frame count (camera paths); 0 = timed */
    int capture_frame;          /* frame drawn for the saved image */
    char note[64];              /* extra key=value text for the result line */
    void *p;                    /* test-private state */
} tctx;

typedef struct test_def {
    const char *id;             /* <= 7 characters: image names are 8.3 */
    const char *group;          /* synth, model, level */
    const char *what;
    int flags;
    int param;
    const char *file;           /* scene file, for data-driven tests */
    int  (*setup)(tctx *t);     /* 0 ok, 1 skip (t->note says why), <0 fail */
    void (*frame)(tctx *t, int f);
    void (*done)(tctx *t);
} test_def;

enum { T_GL_ONLY = 1, T_DATA = 2 };

/* Test tables (NULL-terminated by a zero id). */
extern const test_def synth_tests[];
extern const test_def model_tests[];
extern const test_def level_tests[];

/* Helpers shared by the tests. */
void db_state_default(rb_state *s);             /* depth on, no blend, no texture */
void db_projection(mat4 *p, float fovy, float znear, float zfar);
/* A modelview that maps pixel coordinates (x right, y down) at eye distance
 * d onto the screen with the standard projection (fovy 60). */
void db_pixel_view(mat4 *mv, float d);
#define DB_FOVY  60.0f
#define DB_ZNEAR 1.0f
#define DB_ZFAR  4096.0f
/* Deterministic procedural texture (checker with a gradient); kind 0..3. */
void db_texture(uint8_t *rgba, int w, int h, int kind, int seed);
char *db_path(const char *file);                /* data directory + file */

/* results.c */
int  res_open(const char *dir);
void res_close(void);
/* One line to RESULTS.TXT and the same as an HX-STAT line. */
void res_line(const char *fmt, ...);

#endif
