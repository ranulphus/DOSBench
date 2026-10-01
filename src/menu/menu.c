/* menu.c - DBMENU.EXE, DOSBench's text-mode front end (DJGPP, conio).
 *
 * Pick the APIs, modes, options and tests, then run: the menu writes
 * TESTS.LST, RUN.ARG and RUNSEL.BAT and exits with code 2, and DOSBENCH.BAT runs
 * RUNSEL.BAT (each benchmark program on its own, with nothing resident
 * from the menu) and restarts the menu. "Results" reads OUT\RESULTS.TXT and
 * shows the latest figures per test and program. Selections are kept in
 * DBMENU.CFG. Paths are relative to the DOSBench directory. */
#include <conio.h>
#include <stdarg.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char *id, *group, *what; int gl_only; const char *metric, *unit; } menu_test;
/* The test catalogue: the registry (src/core/tests.json), in run order. */
static const menu_test menu_tests[] = {
#define DB_TEST(id, group, parent, impl, param, file, flags, metric, unit, weight, secs, derive, title, what) \
    { id, group, what, (flags) & 1, metric, unit },
#include "registry.h"
    { 0, 0, 0, 0, 0, 0 }
};

#define MAX_TESTS 96
#define LIST_TOP 3
#define LIST_ROWS 18

#define NMODES 10
enum { O_GL, O_GLIDE, O_M0, O_VSYNC = O_M0 + NMODES, O_SUBMIT, O_SECS, O_SHOTS, O_COUNT };
static const char *const submit_names[] = { "arrays", "lists", "immediate" };
static const int secs_values[] = { 0, 3, 5, 10, 20 };   /* 0: the registry's (3 s per feature phase, 5 s per model) */
static const char *const mode_names[NMODES] = { "320x200", "320x240", "400x300", "512x384", "640x480",
                                                "640x512", "800x600", "1024x768", "1280x1024", "1600x1200" };

static int opt[O_COUNT] = { 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };   /* GL, Glide, 640x480, auto */
static int sel[MAX_TESTS];
static int ntests;

enum { A_TITLE = 0x1F, A_TEXT = 0x07, A_DIM = 0x08, A_HEAD = 0x0E, A_CUR = 0x70, A_KEY = 0x0B };

static void put(int x, int y, int attr, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    gotoxy(x, y);
    textattr(attr);
    cputs(buf);
}

static void line(int y, int attr, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    put(1, y, attr, "%-80.80s", buf);
}

/* ---- Settings ---------------------------------------------------------- */
static void load_cfg(void)
{
    FILE *f = fopen("DBMENU.CFG", "r");
    char buf[1024];
    int i;
    for (i = 0; i < ntests; i++)
        sel[i] = 1;
    if (!f)
        return;
    while (fgets(buf, sizeof buf, f)) {
        char *v = strchr(buf, '=');
        if (!v)
            continue;
        *v++ = 0;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(buf, "OPT")) {
            for (i = 0; i < O_COUNT && v[i]; i++)
                opt[i] = v[i] - '0';
        } else if (!strcmp(buf, "TESTS")) {
            for (i = 0; i < ntests; i++) {
                const char *p = strstr(v, menu_tests[i].id);
                size_t n = strlen(menu_tests[i].id);
                sel[i] = p && (p == v || p[-1] == ',') && (p[n] == ',' || p[n] == 0);
            }
        }
    }
    fclose(f);
}

static void save_cfg(void)
{
    FILE *f = fopen("DBMENU.CFG", "w");
    int i;
    if (!f)
        return;
    fprintf(f, "OPT=");
    for (i = 0; i < O_COUNT; i++)
        fputc('0' + opt[i], f);
    fprintf(f, "\nTESTS=");
    for (i = 0; i < ntests; i++)
        if (sel[i])
            fprintf(f, "%s,", menu_tests[i].id);
    fprintf(f, "\n");
    fclose(f);
}

/* ---- Main screen ------------------------------------------------------- */
static int rows(void) { return O_COUNT + 1 + ntests; }

static void row_text(int r, char *buf, size_t n)
{
    if (r < O_COUNT) {
        switch (r) {
        case O_GL: snprintf(buf, n, " [%c] OpenGL on DOS-GL        (BENCHGL.EXE)", opt[r] ? 'x' : ' '); break;
        case O_GLIDE: snprintf(buf, n, " [%c] Glide via GLIDE2X.OVL   (BENCHG.EXE)", opt[r] ? 'x' : ' '); break;
        case O_M0: case O_M0 + 1: case O_M0 + 2: case O_M0 + 3: case O_M0 + 4:
        case O_M0 + 5: case O_M0 + 6: case O_M0 + 7: case O_M0 + 8: case O_M0 + 9:
            snprintf(buf, n, " [%c] Mode %s", opt[r] ? 'x' : ' ', mode_names[r - O_M0]);
            break;
        case O_VSYNC: snprintf(buf, n, "     Vsync: %s", opt[r] ? "on" : "off (measure the hardware)"); break;
        case O_SUBMIT: snprintf(buf, n, "     OpenGL static geometry: %s", submit_names[opt[r]]); break;
        case O_SECS:
            if (secs_values[opt[r]])
                snprintf(buf, n, "     Seconds per timed test: %d", secs_values[opt[r]]);
            else
                snprintf(buf, n, "     Seconds per timed test: auto (3 per feature phase, 5 per model)");
            break;
        default: snprintf(buf, n, "     Save a frame per test: %s", opt[r] ? "yes (OUT\\*.PPM)" : "no"); break;
        }
    } else if (r == O_COUNT) {
        snprintf(buf, n, " Tests");
    } else {
        const menu_test *t = &menu_tests[r - O_COUNT - 1];
        snprintf(buf, n, " [%c] %-7s %-6s %s%s", sel[r - O_COUNT - 1] ? 'x' : ' ', t->id, t->group, t->what,
                 t->gl_only ? " (OpenGL only)" : "");
    }
}

static void draw_main(int cur, int top)
{
    int i;
    char buf[100];
    line(1, A_TITLE, " DOSBench " "0.1" " - Glide 2.x and OpenGL 1.1 benchmark for DOS");
    line(2, A_TEXT, "");
    for (i = 0; i < LIST_ROWS; i++) {
        int r = top + i;
        if (r >= rows()) {
            line(LIST_TOP + i, A_TEXT, "");
            continue;
        }
        row_text(r, buf, sizeof buf);
        line(LIST_TOP + i, r == cur ? A_CUR : r == O_COUNT ? A_HEAD : A_TEXT, "%s", buf);
    }
    line(22, A_TEXT, "");
    line(23, A_KEY, " Up/Down PgUp/PgDn move   Space toggle   A all tests   N no tests");
    line(24, A_KEY, " R run   V results   Q quit");
    gotoxy(1, 25);
}

static void toggle(int r)
{
    if (r < O_COUNT) {
        if (r == O_SUBMIT)
            opt[r] = (opt[r] + 1) % 3;
        else if (r == O_SECS)
            opt[r] = (opt[r] + 1) % (int)(sizeof secs_values / sizeof secs_values[0]);
        else
            opt[r] = !opt[r];
    } else if (r > O_COUNT) {
        sel[r - O_COUNT - 1] = !sel[r - O_COUNT - 1];
    }
}

/* ---- Running ------------------------------------------------------------ */
static int write_run(void)
{
    FILE *f;
    char modes[128] = "";
    int i, any = 0;
    for (i = 0; i < NMODES; i++)
        if (opt[O_M0 + i]) {
            if (modes[0])
                strcat(modes, ",");
            strcat(modes, mode_names[i]);
        }
    if (!modes[0])
        strcpy(modes, "640x480");
    f = fopen("TESTS.LST", "w");
    if (!f)
        return -1;
    for (i = 0; i < ntests; i++)
        if (sel[i]) {
            fprintf(f, "%s\n", menu_tests[i].id);
            any = 1;
        }
    fclose(f);
    if (!any || (!opt[O_GL] && !opt[O_GLIDE]))
        return -1;
    /* The options go in RUN.ARG: DOS command lines are too short for them. */
    f = fopen("RUN.ARG", "w");
    if (!f)
        return -1;
    fprintf(f, "--tests-from TESTS.LST --data DATA --out OUT --noexit --modes %s --secs %d --submit %s%s%s "
            "--session %08lx\n", modes, secs_values[opt[O_SECS]], submit_names[opt[O_SUBMIT]],
            opt[O_VSYNC] ? " --vsync" : "", opt[O_SHOTS] ? " --shots" : "", (unsigned long)time(NULL));
    fclose(f);
    f = fopen("RUNSEL.BAT", "w");
    if (!f)
        return -1;
    fprintf(f, "@ECHO OFF\r\n");
    if (opt[O_GL])
        fprintf(f, "BENCHGL.EXE --args RUN.ARG\r\n");
    if (opt[O_GLIDE])
        fprintf(f, "BENCHG.EXE --args RUN.ARG --glide=GLIDE2X.OVL\r\n");
    fprintf(f, "DBMENU.EXE --results\r\n");          /* this run's results, then back to the menu */
    fclose(f);
    return 0;
}

/* ---- Results -------------------------------------------------------------- */
/* OUT\RESULTS.TXT: H lines (a program run and mode: run id, tag, session),
 * T lines (a test: run id, test, figures) and S lines (the score). The
 * session screen after a run shows one session (the menu's --session); the
 * V screen shows the latest of everything. */
typedef struct {
    char tag, test[8], mode[10], status[8];
    double fps, metric, p99;
} res_t;

typedef struct {
    char tag, mode[10], status[12], missing[64];
    double score;
} score_t;

static res_t res[400];
static int nres;
static score_t scores[24];
static int nscores;
static char tags[8], modes_seen[4][10];
static int nmodes;

/* The registry's suites, for headings. */
typedef struct { const char *id, *group, *title; } menu_suite;
static const menu_suite menu_suites[] = {
#define DB_SUITE(id, group, title, what, headline) { id, group, title },
#include "registry.h"
    { 0, 0, 0 }
};
static const char *const menu_titles[][3] = {        /* id, parent, title */
#define DB_TEST(id, group, parent, impl, param, file, flags, metric, unit, weight, secs, derive, title, what) \
    { id, parent, title },
#include "registry.h"
    { 0, 0, 0 }
};
static const char *const menu_groups[][2] = {
#define DB_GROUP(id, title) { id, title },
#include "registry.h"
    { 0, 0 }
};

/* The T-line key a test is judged by, from the registry; NULL for frame rates. */
static const char *metric_key(const char *test, const char **unit)
{
    int i;
    *unit = "";
    for (i = 0; menu_tests[i].id; i++)
        if (!strcmp(menu_tests[i].id, test)) {
            if (!strcmp(menu_tests[i].metric, "fps"))
                return NULL;
            *unit = menu_tests[i].unit;
            return menu_tests[i].metric;
        }
    return NULL;
}

static double field(const char *line, const char *key)
{
    char pat[24];
    const char *p;
    snprintf(pat, sizeof pat, " %s=", key);
    p = strstr(line, pat);
    return p ? atof(p + strlen(pat)) : -1;
}

static void sfield(const char *line, const char *key, char *out, int n)
{
    char pat[24];
    const char *p;
    int i = 0;
    snprintf(pat, sizeof pat, " %s=", key);
    p = strstr(line, pat);
    out[0] = 0;
    if (!p)
        return;
    p += strlen(pat);
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < n - 1)
        out[i++] = *p++;
    out[i] = 0;
}

static void add_tag(char t)
{
    if (!strchr(tags, t) && strlen(tags) < 3) {
        size_t n = strlen(tags);
        tags[n] = t;
        tags[n + 1] = 0;
    }
}

static void add_mode(const char *m)
{
    int i;
    for (i = 0; i < nmodes; i++)
        if (!strcmp(modes_seen[i], m))
            return;
    if (nmodes < 4)
        strcpy(modes_seen[nmodes++], m);
}

/* The latest session in the file (the last H line's). */
static void last_session(char *out, int n)
{
    FILE *f = fopen("OUT\\RESULTS.TXT", "r");
    char buf[600];
    out[0] = 0;
    if (!f)
        return;
    while (fgets(buf, sizeof buf, f))
        if (!strncmp(buf, "H ", 2))
            sfield(buf, "session", out, n);
    fclose(f);
}

/* Read OUT\RESULTS.TXT: every record, or only session's (its H lines name
 * the run ids that belong to it). Later records replace earlier ones. */
static void load_results(const char *session)
{
    FILE *f = fopen("OUT\\RESULTS.TXT", "r");
    static char runs[32][12];
    int nruns = 0;
    char buf[600], run[12], tag[4], s[16];
    nres = nscores = nmodes = 0;
    tags[0] = 0;
    if (!f)
        return;
    while (fgets(buf, sizeof buf, f)) {
        int i, mine = !session;
        sfield(buf, "run", run, sizeof run);
        if (session && !strncmp(buf, "H ", 2)) {
            sfield(buf, "session", s, sizeof s);
            if (!strcmp(s, session) && nruns < 32)
                strcpy(runs[nruns++], run);
        }
        for (i = 0; !mine && i < nruns; i++)
            mine = !strcmp(runs[i], run);
        if (!mine)
            continue;
        sfield(buf, "tag", tag, sizeof tag);
        if (!strncmp(buf, "T ", 2)) {
            res_t r;
            memset(&r, 0, sizeof r);
            r.tag = tag[0];
            sfield(buf, "test", r.test, sizeof r.test);
            sfield(buf, "mode", r.mode, sizeof r.mode);
            sfield(buf, "status", r.status, sizeof r.status);
            r.fps = field(buf, "fps");
            r.p99 = field(buf, "p99_ms");
            r.metric = -1;
            {
                const char *unit, *key = metric_key(r.test, &unit);
                if (key)
                    r.metric = field(buf, key);
            }
            add_tag(r.tag);
            add_mode(r.mode);
            for (i = 0; i < nres; i++)
                if (res[i].tag == r.tag && !strcmp(res[i].test, r.test) && !strcmp(res[i].mode, r.mode))
                    break;
            if (i < nres)
                res[i] = r;
            else if (nres < (int)(sizeof res / sizeof res[0]))
                res[nres++] = r;
        } else if (!strncmp(buf, "S ", 2)) {
            score_t c;
            memset(&c, 0, sizeof c);
            c.tag = tag[0];
            sfield(buf, "mode", c.mode, sizeof c.mode);
            sfield(buf, "status", c.status, sizeof c.status);
            sfield(buf, "missing", c.missing, sizeof c.missing);
            c.score = field(buf, "score");
            add_tag(c.tag);
            add_mode(c.mode);
            for (i = 0; i < nscores; i++)
                if (scores[i].tag == c.tag && !strcmp(scores[i].mode, c.mode))
                    break;
            if (i < nscores)
                scores[i] = c;
            else if (nscores < (int)(sizeof scores / sizeof scores[0]))
                scores[nscores++] = c;
        }
    }
    fclose(f);
}

static const char *tag_name(char t)
{
    return t == 'L' ? "OpenGL" : t == 'G' ? "Glide" : t == 'V' ? "Glide (3dfx)" : "?";
}

/* label cut to w columns; "triangles" becomes "tris" when it does not fit. */
static void fit(char *out, int w, const char *label)
{
    char buf[64];
    const char *p = strstr(label, "triangles");
    if ((int)strlen(label) > w && p)
        snprintf(buf, sizeof buf, "%.*stris%s", (int)(p - label), label, p + 9);
    else
        snprintf(buf, sizeof buf, "%s", label);
    snprintf(out, (size_t)w + 1, "%s", buf);
}

/* A frame rate, average/1% low, in a 15-column cell: decimals go first, then the 1% low. */
static void fps_cell(char *cell, int n, double fps, double p99)
{
    double low = p99 > 0 ? 1000.0 / p99 : 0.0;
    snprintf(cell, n, "%.1f/%.1f fps", fps, low);
    if (strlen(cell) > 15)
        snprintf(cell, n, "%.0f/%.0f fps", fps, low);
    if (strlen(cell) > 15)
        snprintf(cell, n, "%.0f fps", fps);
}

/* The screen as text lines: score first, then every test in run order under
 * its group or suite. Returns the line count. */
static char out_lines[200][104];
static int build_lines(void)
{
    int n = 0, i, t, m, ntags = (int)strlen(tags);
    int lw = 80 - 1 - 3 - 16 * (ntags ? ntags : 1);   /* the label column */
    const char *heading = "";
#define OUT(...) do { if (n < 200) snprintf(out_lines[n++], sizeof out_lines[0], __VA_ARGS__); } while (0)
    if (lw > 40)
        lw = 40;
    {
        char row[100];
        int x = snprintf(row, sizeof row, " %-*s", lw + 2, "DOSBench score");
        for (t = 0; tags[t]; t++)
            x += snprintf(row + x, sizeof row - x, " %-15.15s", tag_name(tags[t]));
        OUT("%s", row);
    }
    for (m = 0; m < nmodes && nscores; m++) {
        char row[100], why[64] = "";
        int x = snprintf(row, sizeof row, "   %-*s", lw, modes_seen[m]);
        for (t = 0; tags[t]; t++) {
            char cell[24] = "-";
            for (i = 0; i < nscores; i++)
                if (scores[i].tag == tags[t] && !strcmp(scores[i].mode, modes_seen[m])) {
                    if (scores[i].score > 0)
                        snprintf(cell, sizeof cell, "%.0f%s", scores[i].score,
                                 strcmp(scores[i].status, "quick") ? "" : " (quick)");
                    else {
                        snprintf(cell, sizeof cell, "%s", scores[i].status);
                        if (scores[i].missing[0] && !why[0])
                            snprintf(why, sizeof why, "missing %s", scores[i].missing);
                    }
                }
            x += snprintf(row + x, sizeof row - x, " %-15.15s", cell);
        }
        OUT("%s", row);
        if (why[0])
            OUT("     %s", why);
    }
    if (!nscores)
        OUT("   no score: the game scenes make it, and none ran");
    OUT("%s", "");
    for (i = 0; menu_titles[i][0]; i++) {
        const char *id = menu_titles[i][0], *parent = menu_titles[i][1];
        const char *head = parent;
        int any = 0, k;
        for (k = 0; k < nres; k++)
            any |= !strcmp(res[k].test, id);
        if (!any)
            continue;
        if (!head[0]) {                         /* not a phase: its group heads it */
            for (k = 0; menu_tests[k].id; k++)
                if (!strcmp(menu_tests[k].id, id))
                    head = menu_tests[k].group;
        }
        if (strcmp(head, heading)) {
            const char *title = head;
            for (k = 0; menu_suites[k].id; k++)
                if (!strcmp(menu_suites[k].id, head))
                    title = menu_suites[k].title;
            for (k = 0; menu_groups[k][0]; k++)
                if (!strcmp(menu_groups[k][0], head))
                    title = menu_groups[k][1];
            OUT(" %s", title);
            heading = head;
        }
        for (m = 0; m < nmodes; m++) {
            char row[100], label[48];
            int x;
            if (nmodes > 1) {
                char mode[12];
                snprintf(mode, sizeof mode, " %.9s", modes_seen[m]);
                fit(label, lw - (int)strlen(mode), menu_titles[i][2]);
                strcat(label, mode);
            } else
                fit(label, lw, menu_titles[i][2]);
            x = snprintf(row, sizeof row, "   %-*s", lw, label);
            for (t = 0; tags[t]; t++) {
                char cell[24] = "-";
                for (k = 0; k < nres; k++)
                    if (res[k].tag == tags[t] && !strcmp(res[k].test, id) && !strcmp(res[k].mode, modes_seen[m])) {
                        if (strcmp(res[k].status, "ok"))
                            snprintf(cell, sizeof cell, "%s", res[k].status);
                        else if (res[k].metric >= 0) {
                            const char *unit;
                            metric_key(id, &unit);
                            snprintf(cell, sizeof cell, "%.1f %s", res[k].metric, unit);
                        } else
                            fps_cell(cell, sizeof cell, res[k].fps, res[k].p99);
                    }
                x += snprintf(row + x, sizeof row - x, " %-15.15s", cell);
            }
            OUT("%s", row);
        }
    }
    if (!nres)
        OUT(" No results yet: run some tests first.");
    else
        OUT(" (frame rates: average/1%% low)");
#undef OUT
    return n;
}

static void write_lines(const char *path, int n, const char *title)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f)
        return;
    fprintf(f, "%s\n", title);
    for (i = 0; i < n; i++)
        fprintf(f, "%s\n", out_lines[i]);
    fclose(f);
}

/* session NULL: the latest of everything (V); else that session's results
 * (after a run), also written to OUT\SUMMARY.TXT. */
static void show_results(const char *session)
{
    int n, top = 0, ch, i;
    char title[80];
    load_results(session);
    n = build_lines();
    if (session)
        snprintf(title, sizeof title, " DOSBench results: this run (session %s)", session);
    else
        snprintf(title, sizeof title, " DOSBench results: the latest of each test (OUT\\RESULTS.TXT)");
    if (session)
        write_lines("OUT\\SUMMARY.TXT", n, title);
    for (;;) {
        line(1, A_TITLE, "%s", title);
        for (i = 0; i < 21; i++) {
            int r = top + i;
            line(2 + i, r < n && (out_lines[r][0] == ' ' && out_lines[r][1] != ' ') ? A_HEAD : A_TEXT,
                 "%s", r < n ? out_lines[r] : "");
        }
        line(23, A_TEXT, "");
        line(24, A_KEY, " Up/Down PgUp/PgDn scroll   Esc back to the menu");
        gotoxy(1, 25);
        ch = getch();
        if (ch == 27 || ch == 'q' || ch == 'Q' || ch == 13)
            return;
        if (ch == 0) {
            ch = getch();
            if (ch == 72 && top > 0) top--;
            if (ch == 80 && top + 21 < n) top++;
            if (ch == 73) top = top > 21 ? top - 21 : 0;
            if (ch == 81 && n > 21) top = top + 21 < n - 21 ? top + 21 : n - 21;
        }
    }
}

int main(int argc, char **argv)
{
    int cur = 0, top = 0, ch, i;
    const char *dump = NULL, *session = NULL;
    int results = 0;
    for (ntests = 0; menu_tests[ntests].id && ntests < MAX_TESTS; ntests++)
        ;
    /* --results [--session ID]: the results screen (after a run: the last
     * session); --dump FILE: the same as text, without the screen (Loop A). */
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--results"))
            results = 1;
        else if (!strcmp(argv[i], "--session") && i + 1 < argc)
            session = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
    }
    if (results || dump) {
        static char last[16];
        if (!session) {
            last_session(last, sizeof last);
            session = last[0] && strcmp(last, "-") ? last : NULL;
        }
        if (dump) {
            load_results(session);
            write_lines(dump, build_lines(), "DOSBench results");
            return 0;
        }
        textmode(C80);
        _setcursortype(_NOCURSOR);
        clrscr();
        show_results(session);
        textattr(A_TEXT);
        clrscr();
        _setcursortype(_NORMALCURSOR);
        return 0;
    }
    load_cfg();
    textmode(C80);
    _setcursortype(_NOCURSOR);
    clrscr();
    for (;;) {
        if (cur < top) top = cur;
        if (cur >= top + LIST_ROWS) top = cur - LIST_ROWS + 1;
        draw_main(cur, top);
        ch = getch();
        if (ch == 0) {
            ch = getch();
            if (ch == 72 && cur > 0) cur--;
            else if (ch == 80 && cur < rows() - 1) cur++;
            else if (ch == 73) cur = cur > LIST_ROWS ? cur - LIST_ROWS : 0;
            else if (ch == 81) cur = cur + LIST_ROWS < rows() ? cur + LIST_ROWS : rows() - 1;
            else if (ch == 71) cur = 0;
            else if (ch == 79) cur = rows() - 1;
            continue;
        }
        switch (ch) {
        case ' ': case 13: toggle(cur); break;
        case 'a': case 'A': for (i = 0; i < ntests; i++) sel[i] = 1; break;
        case 'n': case 'N': for (i = 0; i < ntests; i++) sel[i] = 0; break;
        case 'v': case 'V': clrscr(); show_results(NULL); clrscr(); break;
        case 'r': case 'R':
            save_cfg();
            if (write_run() == 0) {
                textattr(A_TEXT);
                clrscr();
                _setcursortype(_NORMALCURSOR);
                return 2;
            }
            line(22, 0x4F, " Nothing to run: select at least one API and one test.");
            getch();
            break;
        case 'q': case 'Q': case 27:
            save_cfg();
            textattr(A_TEXT);
            clrscr();
            _setcursortype(_NORMALCURSOR);
            return 0;
        default: break;
        }
    }
}
