/* score.c - the DOSBench score (score.h). No rendering: host-tested
 * against tests/fixtures/score.txt, as tools/registry.py's score is.
 *
 * A score needs every scored test of the registry to have run, and run ok,
 * in the same program run and mode: one missing or failed scene gives
 * "incomplete" and names it, never a partial number. Quick runs (a few
 * frames) are marked "quick", and Esc gives "aborted". */
#include "score.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void score_reset(score_acc *a, const test_def *tbl, const char *list)
{
    const test_def *d;
    memset(a, 0, sizeof *a);
    for (d = tbl; d->id && a->n < SCORE_MAX; d++)
        if (d->weight > 0 && !(d->flags & T_SUITE)) {
            a->t[a->n] = d;
            a->selected += reg_selected(d, list);
            a->n++;
        }
}

void score_got(score_acc *a, const test_def *d, const char *status, double fps)
{
    int i;
    for (i = 0; i < a->n; i++)
        if (a->t[i] == d) {
            snprintf(a->status[i], sizeof a->status[i], "%s", status);
            a->fps[i] = fps;
        }
}

double score_of(int n, const int *weight, const double *fps, double scale)
{
    double sw = 0, s = 0;
    int i;
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (weight[i] <= 0 || fps[i] <= 0)
            return 0;
        sw += weight[i];
        s += weight[i] * log(fps[i]);
    }
    return scale * exp(s / sw);
}

int score_line(const score_acc *a, int quick, int aborted, double scale, int ver, char *buf, int n, double *score)
{
    int i, ok = 0, k;
    int w[SCORE_MAX];
    char missing[160] = "";
    size_t m = 0;
    *score = 0;
    if (a->selected == 0)
        return 0;
    for (i = 0; i < a->n; i++) {
        w[i] = a->t[i]->weight;
        if (!strcmp(a->status[i], "ok") && a->fps[i] > 0)
            ok++;
        else
            m += (size_t)snprintf(missing + m, sizeof missing - m, "%s%s:%s", m ? "," : "", a->t[i]->id,
                                  a->status[i][0] ? a->status[i] : "notrun");
    }
    if (ok == a->n && !aborted)
        *score = score_of(a->n, w, a->fps, scale);
    k = snprintf(buf, n, "scorever=%d status=%s", ver,
                 aborted ? "aborted" : ok < a->n ? "incomplete" : quick ? "quick" : "ok");
    if (*score > 0)
        k += snprintf(buf + k, n - k, " score=%.0f", *score);
    k += snprintf(buf + k, n - k, " scenes=%d/%d", ok, a->n);
    for (i = 0; i < a->n && k < n; i++)
        if (!strcmp(a->status[i], "ok"))
            k += snprintf(buf + k, n - k, " %s=%.2f", a->t[i]->id, a->fps[i]);
    if (missing[0] && k < n)
        snprintf(buf + k, n - k, " missing=%s", missing);
    return 1;
}
