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

typedef struct { const char *id, *group, *what; int gl_only; const char *metric, *unit; } menu_test;
/* The test catalogue: the registry (src/core/tests.json), in run order. */
static const menu_test menu_tests[] = {
#define DB_TEST(id, group, parent, impl, param, file, flags, metric, unit, weight, derive, title, what) \
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
static const int secs_values[] = { 3, 5, 10, 20 };
static const char *const mode_names[NMODES] = { "320x200", "320x240", "400x300", "512x384", "640x480",
                                                "640x512", "800x600", "1024x768", "1280x1024", "1600x1200" };

static int opt[O_COUNT] = { 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0 };   /* GL, Glide, 640x480, 5 s */
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
        case O_SECS: snprintf(buf, n, "     Seconds per timed test: %d", secs_values[opt[r]]); break;
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
            opt[r] = (opt[r] + 1) % 4;
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
    fprintf(f, "--tests-from TESTS.LST --data DATA --out OUT --noexit --modes %s --secs %d --submit %s%s%s\n",
            modes, secs_values[opt[O_SECS]], submit_names[opt[O_SUBMIT]], opt[O_VSYNC] ? " --vsync" : "",
            opt[O_SHOTS] ? " --shots" : "");
    fclose(f);
    f = fopen("RUNSEL.BAT", "w");
    if (!f)
        return -1;
    fprintf(f, "@ECHO OFF\r\n");
    if (opt[O_GL])
        fprintf(f, "BENCHGL.EXE --args RUN.ARG\r\n");
    if (opt[O_GLIDE])
        fprintf(f, "BENCHG.EXE --args RUN.ARG --glide=GLIDE2X.OVL\r\n");
    fclose(f);
    return 0;
}

/* ---- Results -------------------------------------------------------------- */
typedef struct {
    char tag, test[8], mode[10];
    double fps, metric, p99;
    const char *unit;
} res_t;

static res_t res[400];
static int nres;
static char tags[8];

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

static void load_results(void)
{
    FILE *f = fopen("OUT\\RESULTS.TXT", "r");
    char buf[600], status[8], tag[4];
    nres = 0;
    tags[0] = 0;
    if (!f)
        return;
    while (fgets(buf, sizeof buf, f)) {
        res_t r;
        const char *key;
        int i;
        if (strncmp(buf, "T ", 2))
            continue;
        sfield(buf, "status", status, sizeof status);
        if (strcmp(status, "ok"))
            continue;
        memset(&r, 0, sizeof r);
        sfield(buf, "tag", tag, sizeof tag);
        r.tag = tag[0];
        sfield(buf, "test", r.test, sizeof r.test);
        sfield(buf, "mode", r.mode, sizeof r.mode);
        r.fps = field(buf, "fps");
        r.p99 = field(buf, "p99_ms");
        key = metric_key(r.test, &r.unit);
        r.metric = key ? field(buf, key) : -1;
        if (!strchr(tags, r.tag) && strlen(tags) < 3) {
            size_t n = strlen(tags);
            tags[n] = r.tag;
            tags[n + 1] = 0;
        }
        for (i = 0; i < nres; i++)          /* the latest run replaces older ones */
            if (res[i].tag == r.tag && !strcmp(res[i].test, r.test) && !strcmp(res[i].mode, r.mode))
                break;
        if (i < nres)
            res[i] = r;
        else if (nres < (int)(sizeof res / sizeof res[0]))
            res[nres++] = r;
    }
    fclose(f);
}

static const char *tag_name(char t)
{
    return t == 'L' ? "OpenGL" : t == 'G' ? "Glide" : t == 'V' ? "Glide (3dfx)" : "?";
}

static void show_results(void)
{
    /* One row per (test, mode); a column per program. */
    static char keys[400][20];
    int nkeys = 0, i, k, top = 0, ch;
    load_results();
    for (i = 0; i < nres; i++) {
        char key[20];
        snprintf(key, sizeof key, "%s %s", res[i].test, res[i].mode);
        for (k = 0; k < nkeys && strcmp(keys[k], key); k++)
            ;
        if (k == nkeys)
            strcpy(keys[nkeys++], key);
    }
    for (;;) {
        line(1, A_TITLE, " DOSBench results (OUT\\RESULTS.TXT, latest run of each test)");
        {
            char head[100];
            int x = snprintf(head, sizeof head, " %-7s %-9s", "test", "mode");
            for (i = 0; tags[i]; i++)
                x += snprintf(head + x, sizeof head - x, " %-20.20s", tag_name(tags[i]));
            line(2, A_HEAD, "%s", head);
        }
        for (i = 0; i < 19; i++) {
            char row[100], test[8], mode[10];
            int r = top + i, x, t;
            if (r >= nkeys) {
                line(3 + i, A_TEXT, nkeys ? "" : (i ? "" : " No results yet: run some tests first."));
                continue;
            }
            sscanf(keys[r], "%7s %9s", test, mode);
            x = snprintf(row, sizeof row, " %-7s %-9s", test, mode);
            for (t = 0; tags[t]; t++) {
                char cell[24] = "-";
                for (k = 0; k < nres; k++)
                    if (res[k].tag == tags[t] && !strcmp(res[k].test, test) && !strcmp(res[k].mode, mode)) {
                        if (res[k].metric >= 0)
                            snprintf(cell, sizeof cell, "%.1f %s %.0ffps", res[k].metric, res[k].unit, res[k].fps);
                        else
                            snprintf(cell, sizeof cell, "%.1f fps p99 %.0fms", res[k].fps, res[k].p99);
                        break;
                    }
                x += snprintf(row + x, sizeof row - x, " %-20.20s", cell);
            }
            line(3 + i, A_TEXT, "%s", row);
        }
        line(23, A_TEXT, "");
        line(24, A_KEY, " Up/Down PgUp/PgDn scroll   Esc back");
        gotoxy(1, 25);
        ch = getch();
        if (ch == 27 || ch == 'q' || ch == 'Q')
            return;
        if (ch == 0) {
            ch = getch();
            if (ch == 72 && top > 0) top--;
            if (ch == 80 && top + 19 < nkeys) top++;
            if (ch == 73) top = top > 19 ? top - 19 : 0;
            if (ch == 81 && nkeys > 19) top = top + 19 < nkeys - 19 ? top + 19 : nkeys - 19;
        }
    }
}

int main(void)
{
    int cur = 0, top = 0, ch, i;
    for (ntests = 0; menu_tests[ntests].id && ntests < MAX_TESTS; ntests++)
        ;
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
        case 'v': case 'V': clrscr(); show_results(); clrscr(); break;
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
