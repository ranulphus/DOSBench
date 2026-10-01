/* bench.c - DOSBench's program core: options, the test runner, results.
 *
 * Built twice: BENCHG.EXE (Open Watcom, Glide through rb_glide.c) and
 * BENCHGL.EXE (DJGPP, OpenGL through rb_gl.c). Each test draws warm-up
 * frames, then measured frames (a fixed count for camera paths, otherwise
 * until the target time has passed), timing every frame with the TSC:
 * frame-to-frame time, and the time spent submitting before the swap. After
 * measuring it may draw one more, fixed frame and save it for image checks.
 *
 *   BENCHG [--tests LIST | --tests-from FILE] [--modes WxH,...|all] [--quick] [--shots] [--vsync]
 *          [--args FILE (more arguments from a file)]
 *          [--submit arrays|lists|immediate] [--secs S] [--data DIR]
 *          [--tag C] [--shot-frames F,...] [--list] [--glide=PATH] [--out DIR]
 *          [--captions S] [--noexit]
 */
#include "bench.h"
#include "present.h"
#include "ov.h"
#include "stats.h"
#include "timer.h"
#include "hx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DB_PROG
#  define DB_PROG "BENCH"
#endif

db_opts db;

static const char *tests_arg = "all";
static const char *shot_frames;         /* --shot-frames a,b,...: extra saved frames */
static const char *modes_arg = "640x480";
static int list_only;
static char run_id[12];

static int is_glide(void)
{
    return !strcmp(rb_get_info()->api, "glide");
}

/* ---- Shared helpers -------------------------------------------------- */
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
    snprintf(buf, sizeof buf, "%s\\%s", db.data, file);
    return buf;
}

static void sanitise(char *s)
{
    for (; *s; s++)
        if (*s == ' ' || *s == '\t' || *s == '=')
            *s = '_';
}

/* ---- Selection -------------------------------------------------------- */
static char tests_buf[1024];

/* --tests-from FILE reads the list from a file (DOS command lines stop at
 * 126 characters; DJGPP expands @FILE arguments itself): names separated by
 * commas, spaces or line breaks. */
static const char *tests_from;

static void load_tests_arg(void)
{
    FILE *f;
    size_t n;
    char *c;
    if (!tests_from)
        return;
    f = fopen(tests_from, "r");
    if (!f) {
        hx_log("cannot read test list %s", tests_from);
        tests_arg = "";
        return;
    }
    n = fread(tests_buf, 1, sizeof tests_buf - 1, f);
    fclose(f);
    tests_buf[n] = 0;
    for (c = tests_buf; *c; c++)
        if (*c == ' ' || *c == '\r' || *c == '\n' || *c == '\t')
            *c = ',';
    tests_arg = tests_buf;
}

static const char *submit_name(int s)
{
    return s == RB_SUBMIT_LISTS ? "lists" : s == RB_SUBMIT_IMMEDIATE ? "immediate" : "arrays";
}

/* ---- Runner ------------------------------------------------------------ */
static void result(const tctx *t, const char *status, int n, double total_ms, const st_summary *s,
                   double submit_ms, unsigned long tex_bytes, uint32_t crc)
{
    const test_def *d = t->def;
    double secs = total_ms / 1000.0;
    char rates[160] = "";
    if (n > 0 && secs > 0) {
        int k = 0;
        if (t->tris > 0)
            k += snprintf(rates + k, sizeof rates - k, " ktris_s=%.1f", t->tris * n / secs / 1000.0);
        if (t->pixels > 0)
            k += snprintf(rates + k, sizeof rates - k, " mpix_s=%.2f", t->pixels * n / secs / 1e6);
        if (t->texels > 0)
            k += snprintf(rates + k, sizeof rates - k, " mtexel_s=%.2f", t->texels * n / secs / 1e6);
        snprintf(rates + k, sizeof rates - k, " tex_kb_frame=%.1f", tex_bytes / 1024.0 / n);
    }
    char parent[24] = "";
    if (d->parent[0])                   /* a phase of a suite */
        snprintf(parent, sizeof parent, " parent=%s", d->parent);
    res_line("T run=%s prog=%s tag=%c api=%s mode=%dx%d test=%s group=%s%s status=%s frames=%d secs=%.3f "
             "fps=%.2f avg_ms=%.3f med_ms=%.3f p99_ms=%.3f min_ms=%.3f max_ms=%.3f submit_ms=%.3f "
             "tris_frame=%.0f%s crc=%08lx%s%s",
             run_id, DB_PROG, db.tag, rb_get_info()->api, db.w, db.h, d->id, d->group, parent, status, n, secs,
             s ? s->fps : 0.0, s ? s->avg_ms : 0.0, s ? s->med_ms : 0.0, s ? s->p99_ms : 0.0,
             s ? s->min_ms : 0.0, s ? s->max_ms : 0.0, submit_ms, t->tris, rates, (unsigned long)crc,
             t->note[0] ? " " : "", t->note);
}

/* Run one test (a phase gets its caption first: phase i of ni, test k of
 * n); what it measured goes to *out for the cards. */
static void run_test(const test_def *d, int i, int ni, int k, int kn, const db_result *before, db_result *out)
{
    tctx t;
    int rc, f, n = 0, cap = db.max_frames;
    double secs = db.secs > 0 ? db.secs : d->secs > 0 ? d->secs : 5.0;
    double *ft, *sub, submit = 0, total;
    tmr_t t0, prev, a, b, c;
    unsigned long tex0;
    uint32_t crc = 0;
    st_summary s;
    memset(&t, 0, sizeof t);
    t.def = d;
    memset(out, 0, sizeof *out);
    out->d = d;
    strcpy(out->status, "skip");
    if ((d->flags & T_GL_ONLY) && is_glide()) {
        strcpy(t.note, "why=gl-only");
        hx_log("HX-TEST %s SKIP gl-only", d->id);
        result(&t, "skip", 0, 0, NULL, 0, 0, 0);
        return;
    }
    rc = d->impl->setup(&t);
    if (rc != 0) {
        hx_log("HX-TEST %s %s %s", d->id, rc > 0 ? "SKIP" : "FAIL", t.note);
        if (rc < 0)
            hx_test(d->id, 0, "setup failed %s", t.note);
        result(&t, rc > 0 ? "skip" : "fail", 0, 0, NULL, 0, 0, 0);
        if (rc < 0)
            strcpy(out->status, "fail");
        return;
    }
    if (d->parent[0] && pr_caption(d, &t, i, ni, k, kn, before))
        db.aborted = 1;                 /* Esc: this test still runs */
    if (t.frames > 0)                   /* a camera path: fixed frames (a few in --quick) */
        cap = db.quick && t.frames > db.max_frames ? db.max_frames : t.frames;
    ft = (double *)malloc((size_t)cap * sizeof *ft);
    sub = (double *)malloc((size_t)cap * sizeof *sub);
    if (!ft || !sub) {
        free(ft); free(sub);
        hx_test(d->id, 0, "out of memory");
        if (d->impl->done) d->impl->done(&t);
        return;
    }
    for (f = 0; f < db.warm; f++) {
        d->impl->frame(&t, f);
        rb_swap();
    }
    rb_finish();
    tex0 = rb_tex_bytes();
    t.tris_drawn = 0;
    t0 = prev = tmr_now();
    for (;;) {
        a = tmr_now();
        d->impl->frame(&t, n);
        b = tmr_now();
        rb_swap();
        c = tmr_now();
        ft[n] = tmr_ms(c - prev);
        sub[n] = tmr_ms(b - a);
        submit += sub[n];
        prev = c;
        n++;
        if (t.frames > 0 ? n >= cap
                         : n >= cap || (n >= db.min_frames && tmr_ms(c - t0) >= secs * 1000.0))
            break;
    }
    rb_finish();
    total = tmr_ms(tmr_now() - t0);
    if (t.tris_drawn > 0)
        t.tris = t.tris_drawn / n;      /* varies per frame: the average */
    st_summarise(ft, n, &s);
    if (db.shots && db.w == db.shot_w && db.h == db.shot_h) {
        long bytes = (long)db.w * db.h * 3;
        uint8_t *rgb = (uint8_t *)malloc((size_t)bytes);
        d->impl->frame(&t, t.capture_frame);
        if (rgb && rb_read(rgb) == 0) {
            char name[16];
            snprintf(name, sizeof name, "%c%s", db.tag, d->id);
            crc = hx_crc32(0, rgb, (uint32_t)bytes);
            hx_save_ppm(name, db.w, db.h, rgb);
        }
        free(rgb);
        rb_swap();
    }
    if (shot_frames && strlen(d->id) <= 6 && db.w == db.shot_w && db.h == db.shot_h) {
        long bytes = (long)db.w * db.h * 3;
        uint8_t *rgb = (uint8_t *)malloc((size_t)bytes);
        const char *p = shot_frames;
        int k = 0;
        while (rgb && *p && k < 10) {
            int fr = atoi(p);
            char name[16];
            if (t.frames > 0)
                fr %= t.frames;
            d->impl->frame(&t, fr);
            snprintf(name, sizeof name, "%c%s%d", db.tag, d->id, k++);
            if (rb_read(rgb) == 0)
                hx_save_ppm(name, db.w, db.h, rgb);
            rb_swap();
            while (*p && *p != ',')
                p++;
            if (*p == ',')
                p++;
        }
        free(rgb);
    }
    result(&t, "ok", n, total, &s, n ? submit / n : 0, rb_tex_bytes() - tex0, crc);
    hx_test(d->id, 1, "frames=%d fps=%.2f", n, s.fps);
    strcpy(out->status, "ok");
    out->fps = s.fps;
    out->p99_ms = s.p99_ms;
    {
        double sec = total / 1000.0;
        out->value = !strcmp(d->metric, "mpix_s") ? t.pixels * n / sec / 1e6 :
                     !strcmp(d->metric, "ktris_s") ? t.tris * n / sec / 1000.0 :
                     !strcmp(d->metric, "mtexel_s") ? t.texels * n / sec / 1e6 : s.fps;
    }
    free(ft);
    free(sub);
    if (d->impl->done)
        d->impl->done(&t);
}

static int run_mode(int w, int h, int optional)
{
    char err[128] = "", impl[64], card[40], disp[16];
    const rb_info *in;
    const test_def *d;
    if (rb_open(w, h, db.vsync, err, sizeof err) < 0) {
        if (optional) {                         /* --modes all: a size this card cannot show */
            hx_log("HX-STAT skip mode %dx%d: %s", w, h, err);
            return 0;
        }
        hx_test("open", 0, "%dx%d: %s", w, h, err);
        return -1;
    }
    db.w = w;
    db.h = h;
    in = rb_get_info();
    snprintf(impl, sizeof impl, "%s", in->impl);
    snprintf(card, sizeof card, "%s", in->card);
    sanitise(impl);
    sanitise(card);
    if (in->display_w)
        snprintf(disp, sizeof disp, "%dx%d", in->display_w, in->display_h);
    else
        strcpy(disp, "?");
    res_line("H run=%s prog=%s ver=%s build=%s tag=%c api=%s impl=%s card=%s mode=%dx%d vsync=%d submit=%s "
             "timer=%s cpu_mhz=%.1f tex_kb=%lu quick=%d display=%s fit=%s",
             run_id, DB_PROG, DB_VERSION, DB_BUILD_ID, db.tag, in->api, impl, card, w, h, db.vsync,
             submit_name(db.submit), tmr_source(), tmr_hz() / 1e6, in->tex_mem / 1024, db.quick, disp,
             in->fit[0] ? in->fit : "?");
    rb_set_submit(db.submit);
    if (db.captions > 0 && ov_open() < 0)
        db.captions = 0;                        /* no font texture: no cards */
    {
        int k = 0, kn = reg_count(tests_arg), nres = 0;
        const test_def *suite = NULL;
        db_result last, res[32];
        memset(&last, 0, sizeof last);
        for (d = db_tests; d->id && !db.aborted; d++) {     /* registry order */
            if (d->flags & T_SUITE) {
                if (reg_suite_used(d, tests_arg)) {
                    suite = d;
                    nres = 0;
                    if (pr_title(d, k + 1, kn, &last))
                        db.aborted = 1;
                }
                continue;
            }
            if (!reg_selected(d, tests_arg))
                continue;
            k++;
            if (!d->parent[0] && pr_title(d, k, kn, &last))
                db.aborted = 1;
            {
                int i = 0, ni = 0;              /* its place among the suite's selected phases */
                const test_def *p;
                if (d->parent[0])
                    for (p = db_tests; p->id; p++)
                        if (!strcmp(p->parent, d->parent) && reg_selected(p, tests_arg)) {
                            ni++;
                            if (p == d)
                                i = ni;
                        }
                {
                    db_result prev = last;      /* run_test clears what it writes */
                    run_test(d, i, ni, k, kn, &prev, &last);
                }
                if (suite && d->parent[0] && !strcmp(d->parent, suite->id)) {
                    if (nres < (int)(sizeof res / sizeof res[0]))
                        res[nres++] = last;
                    if (i == ni && pr_summary(suite, res, nres, k, kn))
                        db.aborted = 1;
                }
            }
        }
    }
    if (db.captions > 0)
        ov_close();
    rb_close();
    return 0;
}

/* ---- Options ----------------------------------------------------------- */
static void usage(void)
{
    hx_log("usage: %s [--tests LIST] [--modes WxH,...|all] [--quick] [--shots] [--vsync] "
           "[--submit arrays|lists|immediate] [--secs S] [--data DIR] [--tag C] [--list] [--glide=PATH]",
           DB_PROG);
}

/* --args FILE: more arguments, whitespace-separated, read from a file (DOS
 * passes at most 126 characters, fewer once DOS/4GW adds the program's path). */
static int expand_args(int argc, char **argv, char ***out)
{
    static char buf[2048];
    static char *av[64];
    int n = 0, i;
    size_t used = 0;
    for (i = 0; i < argc && n < 63; i++) {
        if (!strcmp(argv[i], "--args") && i + 1 < argc) {
            FILE *f = fopen(argv[++i], "r");
            size_t got;
            char *p;
            if (!f)
                continue;
            got = fread(buf + used, 1, sizeof buf - used - 1, f);
            fclose(f);
            buf[used + got] = 0;
            for (p = strtok(buf + used, " \t\r\n"); p && n < 63; p = strtok(NULL, " \t\r\n"))
                av[n++] = p;
            used += got + 1;
        } else {
            av[n++] = argv[i];
        }
    }
    av[n] = NULL;
    *out = av;
    return n;
}

int main(int argc, char **argv)
{
    char *hxv[16];
    int hxc = 0, i, bad = 0;
    char err[128] = "";
    argc = expand_args(argc, argv, &argv);
    db.secs = 0;                        /* the registry's: 3 s per feature phase, 5 s per model */
    db.captions = 1.0;
    db.warm = 3;
    db.min_frames = 20;
    db.max_frames = 2000;
    db.data = "C:\\DOSBENCH\\DATA";
    db.tag = DB_PROG[strlen(DB_PROG) - 1] == 'L' ? 'L' : 'G';
    hxv[hxc++] = argv[0];
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--tests") && v) { tests_arg = v; i++; }
        else if (!strcmp(a, "--tests-from") && v) { tests_from = v; i++; }
        else if (!strcmp(a, "--modes") && v) { modes_arg = v; i++; }
        else if (!strcmp(a, "--secs") && v) { db.secs = atof(v); i++; }
        else if (!strcmp(a, "--captions") && v) { db.captions = atof(v); i++; }
        else if (!strcmp(a, "--data") && v) { db.data = v; i++; }
        else if (!strcmp(a, "--tag") && v) { db.tag = v[0]; i++; }
        else if (!strcmp(a, "--shot-frames") && v) { shot_frames = v; i++; }
        else if (!strcmp(a, "--submit") && v) {
            db.submit = !strcmp(v, "lists") ? RB_SUBMIT_LISTS : !strcmp(v, "immediate") ? RB_SUBMIT_IMMEDIATE
                                                                                       : RB_SUBMIT_ARRAYS;
            i++;
        }
        else if (!strcmp(a, "--quick")) db.quick = 1;
        else if (!strcmp(a, "--shots")) db.shots = 1;
        else if (!strcmp(a, "--vsync")) db.vsync = 1;
        else if (!strcmp(a, "--list")) list_only = 1;
        else if (hxc < 15) hxv[hxc++] = argv[i];     /* the test shim's own options */
        else bad = 1;
    }
    hxv[hxc] = NULL;
    hx_init(hxc, hxv, DB_PROG);
    if (bad) {
        usage();
        hx_done(HX_BAD_ARGS);
    }
    db.out = hx_args.out;
    load_tests_arg();
    db.tests = tests_arg;
    if (db.quick) {
        db.warm = 1;
        db.min_frames = db.max_frames = 3;
    }
    if (list_only) {                            /* the registry, in run order */
        const test_def *d;
        for (d = db_tests; d->id; d++)
            hx_log("%-8s %-6s %-8s %s", d->id, d->group, d->flags & T_SUITE ? "suite" : d->parent, d->what);
        hx_done(0);
    }
    if (tmr_init() < 0) {
        hx_test("timer", 0, "the BIOS tick is not running");
        hx_done(HX_INIT_FAILED);
    }
    snprintf(run_id, sizeof run_id, "%08lx", (unsigned long)(tmr_now() & 0xFFFFFFFFu));
    hx_stat("timer source=%s hz=%.0f", tmr_source(), tmr_hz());
    ov_load_font();                     /* the video BIOS's font, before any mode is set */
    if (rb_load(hx_args.glide, err, sizeof err) < 0) {
        hx_test("load", 0, "%s", err);
        hx_done(HX_INIT_FAILED);
    }
    if (res_open(db.out) < 0)
        hx_log("cannot write %s\\RESULTS.TXT; results go to COM1 only", db.out);
    {
        /* Parse every mode first (strtok). */
        char buf[160], *tok;
        int mw[16], mh[16], nm = 0, all = !strcmp(modes_arg, "all");
        /* all: the ten sizes; a card that cannot show one skips it. */
        snprintf(buf, sizeof buf, "%s", all ? "320x200,320x240,400x300,512x384,640x480,640x512,800x600,"
                                              "1024x768,1280x1024,1600x1200" : modes_arg);
        for (tok = strtok(buf, ","); tok && nm < 16; tok = strtok(NULL, ","))
            if (sscanf(tok, "%dx%d", &mw[nm], &mh[nm]) == 2)
                nm++;
            else
                hx_test("mode", 0, "bad mode %s", tok);
        /* Frames are saved by test name alone (8.3 names leave no room for
           the size), so in one mode only: 640x480, which every card and the
           Voodoo can show, or else the first. */
        db.shot_w = nm ? mw[0] : 0;
        db.shot_h = nm ? mh[0] : 0;
        for (i = 0; i < nm; i++)
            if (mw[i] == 640 && mh[i] == 480)
                db.shot_w = 640, db.shot_h = 480;
        for (i = 0; i < nm && !db.aborted; i++)
            run_mode(mw[i], mh[i], all);
    }
    res_close();
    hx_done(0);
    return 0;
}
