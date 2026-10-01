/* present.c - what the screen shows between measurements.
 *
 * A title card before each suite and each test that is not a phase (what it
 * measures, test k of n, the last result), a caption strip over a phase's
 * first frame before it is measured (the phase, the result of the one
 * before), and a card listing a suite's results after its last phase. All
 * of it is drawn outside the timed frames, before the warm-up, so it never
 * reaches a measurement or a saved shot. --captions S holds each for S
 * seconds (cards twice as long); --captions 0 draws none of it; --quick
 * draws each once without holding. Esc during a hold ends the run after the
 * current test. */
#include "present.h"
#include "ov.h"
#include "timer.h"
#include <stdio.h>
#include <string.h>
#if defined(__WATCOMC__) || defined(__DJGPP__)
#include <conio.h>
#define KEY_ESC() (kbhit() && getch() == 27)
#else
#define KEY_ESC() 0
#endif

#define C_BG     0x101828u              /* card background */
#define C_TEXT   0xE8E8E8FFu
#define C_DIM    0x9098A8FFu
#define C_TITLE  0xFFFFFFFFu
#define C_ACCENT 0xF0C040FFu
#define C_STRIP  0x000000B0u
#define C_BAR    0x4080E0FFu

static int body(void) { return db.h >= 600 ? 2 : 1; }

static int hold(double secs)
{
    tmr_t t0;
    if (db.quick || secs <= 0)
        return KEY_ESC();
    t0 = tmr_now();
    while (tmr_ms(tmr_now() - t0) < secs * 1000.0)
        if (KEY_ESC())
            return 1;
    return 0;
}

/* "45.2 fps, 1% low 31.9 fps", "812.4 Mpix/s" or "skipped" */
static void fmt_result(const db_result *r, char *buf, int n)
{
    if (!r->d)
        buf[0] = 0;
    else if (strcmp(r->status, "ok"))
        snprintf(buf, n, "%s", !strcmp(r->status, "skip") ? "skipped" : "failed");
    else if (!strcmp(r->d->metric, "fps"))
        snprintf(buf, n, "%.1f fps, 1%% low %.1f fps", r->fps, r->p99_ms > 0 ? 1000.0 / r->p99_ms : 0.0);
    else
        snprintf(buf, n, "%.1f %s", r->value, r->d->unit);
}

static void header(void)
{
    char buf[128];
    const rb_info *in = rb_get_info();
    snprintf(buf, sizeof buf, "DOSBench %s   %s: %s   %dx%d", DB_VERSION,
             strcmp(in->api, "glide") ? "OpenGL" : "Glide", in->impl, db.w, db.h);
    ov_text(db.w / 32, db.h / 40, 1, C_DIM, buf);
}

static void progress(int k, int n, int y)
{
    char buf[32];
    int x0 = db.w / 16, x1 = db.w - db.w / 16, b = body();
    snprintf(buf, sizeof buf, "test %d of %d", k, n);
    ov_box(x0, y, x1, y + 4 * b, 0x303848FFu);
    if (n > 0)
        ov_box(x0, y, x0 + (x1 - x0) * (k - 1) / n, y + 4 * b, C_BAR);
    ov_text(x1 - ov_text_width(buf, 1), y - OV_CH - 4, 1, C_DIM, buf);
}

static void footer(const db_result *prev, int k, int n)
{
    char res[64], buf[128];
    int y = db.h - db.h / 8;
    if (prev && prev->d) {
        fmt_result(prev, res, sizeof res);
        snprintf(buf, sizeof buf, "%s: %s", prev->d->title, res);
        ov_text(db.w / 16, y - OV_CH - 4, 1, C_DIM, buf);
    }
    progress(k, n, y);
}

int pr_title(const test_def *d, int k, int n, const db_result *prev)
{
    int b = body(), x = db.w / 16, y = db.h / 5, w = db.w - 2 * x, lines;
    if (db.captions <= 0)
        return 0;
    rb_clear(C_BG);
    ov_begin();
    header();
    ov_text(x, y, b, C_ACCENT, db_group_title(d->group));
    y += OV_CH * b * 3 / 2;
    ov_text(x, y, 2 * b, C_TITLE, d->title);
    y += OV_CH * 2 * b * 3 / 2;
    lines = ov_wrap(x, y, w, b, C_TEXT, d->what);
    y += lines * OV_CH * b * 5 / 4 + OV_CH * b;
    if (d->flags & T_SUITE) {               /* the phases it will run */
        const test_def *p;
        for (p = db_tests; p->id && y < db.h - db.h / 4; p++)
            if (!strcmp(p->parent, d->id) && reg_selected(p, db.tests)) {
                char line[96];
                snprintf(line, sizeof line, "- %s", p->title);
                ov_text(x + OV_CW * b, y, 1, C_DIM, line);
                y += OV_CH + 2;
            }
    }
    footer(prev, k, n);
    rb_swap();
    return hold(2 * db.captions);
}

int pr_caption(const test_def *d, tctx *t, int i, int ni, int k, int n, const db_result *prev)
{
    char buf[128], res[64];
    const test_def *s;
    int b = body(), h = 2 * OV_CH * b + 3 * OV_CH, y = db.h - h;
    if (db.captions <= 0)
        return 0;
    d->impl->frame(t, 0);                   /* the phase's first frame, under the strip */
    ov_begin();
    ov_box(0, y, db.w, db.h, C_STRIP);
    for (s = db_tests; s->id && strcmp(s->id, d->parent); s++)
        ;
    snprintf(buf, sizeof buf, "%s: %s  (%d of %d)", s->id ? s->title : d->parent, d->title, i, ni);
    ov_text(db.w / 32, y + OV_CH / 2, b, C_TITLE, buf);
    if (prev && prev->d) {
        fmt_result(prev, res, sizeof res);
        snprintf(buf, sizeof buf, "%s: %s", prev->d->title, res);
        ov_text(db.w / 32, y + OV_CH / 2 + OV_CH * b + 6, 1, C_DIM, buf);
    }
    ov_box(0, db.h - 3, n > 0 ? db.w * (k - 1) / n : 0, db.h, C_BAR);
    rb_swap();
    return hold(db.captions);
}

int pr_summary(const test_def *suite, const db_result *r, int nr, int k, int n)
{
    int b = body(), x = db.w / 16, y = db.h / 5, i;
    if (db.captions <= 0)
        return 0;
    rb_clear(C_BG);
    ov_begin();
    header();
    ov_text(x, y, b, C_ACCENT, db_group_title(suite->group));
    y += OV_CH * b * 3 / 2;
    ov_text(x, y, 2 * b, C_TITLE, suite->title);
    y += OV_CH * 2 * b * 2;
    for (i = 0; i < nr && y < db.h - db.h / 5; i++) {
        char res[64];
        int head = suite->headline[0] && !strcmp(r[i].d->id, suite->headline);
        fmt_result(&r[i], res, sizeof res);
        ov_text(x, y, 1, head ? C_ACCENT : C_TEXT, r[i].d->title);
        ov_text(db.w - x - ov_text_width(res, 1), y, 1, head ? C_ACCENT : C_TEXT, res);
        y += OV_CH + 3;
    }
    progress(k + 1, n, db.h - db.h / 8);
    rb_swap();
    return hold(2 * db.captions);
}
