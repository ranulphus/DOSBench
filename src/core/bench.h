/* bench.h - the benchmark core shared by BENCHG.EXE and BENCHGL.EXE. */
#ifndef BENCH_H
#define BENCH_H
#include <stdint.h>
#include "rb.h"
#include "vmath.h"

#include "version.h"
#ifndef DB_BUILD_ID
#  define DB_BUILD_ID "unknown"
#endif

typedef struct {
    int quick;                  /* --quick: a few frames per test (Loop A) */
    int shots;                  /* --shots: save one frame per test, in one mode */
    int shot_w, shot_h;         /* that mode: 640x480 if run, else the first */
    int vsync;
    int submit;                 /* RB_SUBMIT_* for static geometry */
    double secs;                /* --secs: seconds per timed test (0: the registry's) */
    double captions;            /* --captions: seconds each card or caption is held (0: none) */
    int aborted;                /* Esc on a card: stop after the current test */
    const char *tests;          /* the --tests list */
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

/* The code that draws a test (synth.c, model.c, level.c export these). */
typedef struct test_impl {
    int  (*setup)(tctx *t);     /* 0 ok, 1 skip (t->note says why), <0 fail */
    void (*frame)(tctx *t, int f);
    void (*done)(tctx *t);
} test_impl;

/* A row of the registry (src/core/tests.json, through build/gen/registry.h,
 * in run order): a test, or a suite heading over the tests that name it as
 * their parent (its phases). */
typedef struct test_def {
    const char *id;             /* <= 7 characters: image names are 8.3 */
    const char *group;          /* basic, scene, model, synth, level */
    const char *parent;         /* the suite a phase belongs to, or "" */
    const char *title;          /* short name, for the screen */
    const char *what;
    int flags;
    int param;
    const char *file;           /* scene file, for data-driven tests */
    const char *metric;         /* the T-line key it is judged by */
    const char *unit;
    int weight;                 /* > 0: in the score */
    double secs;                /* timed tests: seconds unless --secs (0: 5) */
    const char *headline;       /* suites: the phase shown for the suite */
    const test_impl *impl;      /* NULL for a suite */
} test_def;

enum { T_GL_ONLY = 1, T_DATA = 2, T_SUITE = 4 };

typedef struct { const char *id, *title, *tests; } db_preset;
typedef struct { const char *id, *title; } db_group;

/* registry.c: every row, ending with a zero id; the presets likewise. */
extern const test_def db_tests[];
extern const db_preset db_presets[];
extern const db_group db_groups[];
const char *db_group_title(const char *group);

/* select.c: whether a test runs for a --tests list: all, a preset, a group,
 * a suite (its phases), an id, or an id prefix of up to 3 characters. */
int reg_selected(const test_def *d, const char *list);
int reg_suite_used(const test_def *suite, const char *list);
int reg_count(const char *list);         /* tests a list runs */

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
