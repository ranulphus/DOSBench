/* test_score.c - score.c against tests/fixtures/score.txt (which
 * tools/registry.py selftest checks too), and the S line's rules. */
#include "unit.h"
#include "score.h"
#include <stdlib.h>
#include <string.h>

/* score_reset needs reg_selected: every test counts as selected unless the
 * list is "none". */
int reg_selected(const test_def *d, const char *list) { (void)d; return strcmp(list, "none") != 0; }

static const test_def tbl[] = {
    { "G1A", "scene", "", "A", "", 0, 0, NULL, "fps", "fps", 1, 0, "", NULL },
    { "S1X", "synth", "", "X", "", 0, 0, NULL, "fps", "fps", 0, 0, "", NULL },
    { "G2B", "scene", "", "B", "", 0, 0, NULL, "fps", "fps", 1, 0, "", NULL },
    { NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, NULL, 0, 0, NULL, NULL }
};

void unit_run(void)
{
    FILE *f = fopen("tests/fixtures/score.txt", "r");
    char line[256], buf[300];
    int cases = 0;
    score_acc a;
    double s;
    CHECK(f != NULL);
    while (f && fgets(line, sizeof line, f)) {
        char *arrow = strstr(line, "->"), *tok;
        int w[16], n = 0;
        double fps[16];
        if (line[0] == '#' || !arrow)
            continue;
        *arrow = 0;
        for (tok = strtok(line, " \t"); tok && n < 16; tok = strtok(NULL, " \t")) {
            w[n] = atoi(tok);
            fps[n] = atof(strchr(tok, ':') + 1);
            n++;
        }
        s = score_of(n, w, fps, 100.0);
        CHECK((long)(s + 0.5) == atol(arrow + 2));         /* rounded, as the S line prints it */
        cases++;
    }
    if (f)
        fclose(f);
    CHECK(cases >= 5);

    score_reset(&a, tbl, "all");
    CHECK(a.n == 2 && a.selected == 2);
    score_got(&a, &tbl[0], "ok", 40);
    score_got(&a, &tbl[1], "ok", 99);          /* not scored: ignored */
    CHECK(score_line(&a, 0, 0, 100, 1, buf, sizeof buf, &s) == 1);
    CHECK(s == 0 && strstr(buf, "status=incomplete") && strstr(buf, "missing=G2B:notrun") && !strstr(buf, "score="));
    score_got(&a, &tbl[2], "ok", 90);
    score_line(&a, 0, 0, 100, 1, buf, sizeof buf, &s);
    CHECK_NEAR(s, 6000, 0.01);
    CHECK(strstr(buf, "status=ok score=6000 scenes=2/2 G1A=40.00 G2B=90.00") != NULL);
    score_line(&a, 1, 0, 100, 1, buf, sizeof buf, &s);
    CHECK(strstr(buf, "status=quick score=6000") != NULL);
    score_line(&a, 0, 1, 100, 1, buf, sizeof buf, &s);
    CHECK(s == 0 && strstr(buf, "status=aborted") != NULL);
    score_got(&a, &tbl[2], "skip", 0);
    score_line(&a, 0, 0, 100, 1, buf, sizeof buf, &s);
    CHECK(strstr(buf, "missing=G2B:skip") != NULL);
    score_reset(&a, tbl, "none");
    CHECK(score_line(&a, 0, 0, 100, 1, buf, sizeof buf, &s) == 0);
}
