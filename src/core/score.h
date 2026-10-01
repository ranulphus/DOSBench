/* score.h - the DOSBench score: 100 x the geometric mean of the scored
 * tests' average frame rates (weights from the registry). */
#ifndef SCORE_H
#define SCORE_H
#include "bench.h"

#define SCORE_MAX 16

typedef struct {
    int n;                              /* scored tests in the registry */
    int selected;                       /* of them, in this run's selection */
    const test_def *t[SCORE_MAX];
    double fps[SCORE_MAX];
    char status[SCORE_MAX][8];          /* "" until run: ok, skip, fail */
} score_acc;

/* Scored tests of table tbl (weight > 0); which the --tests list selects. */
void score_reset(score_acc *a, const test_def *tbl, const char *list);
void score_got(score_acc *a, const test_def *d, const char *status, double fps);
/* scale x exp(sum w ln fps / sum w); 0 if any weight or fps is not positive. */
double score_of(int n, const int *weight, const double *fps, double scale);
/* The S line's fields after run/prog/tag/api/mode: 0 when no scored test was
 * selected (no S line), else 1 with buf = "scorever=.. status=ok|quick|
 * incomplete|aborted [score=..] scenes=k/n [ID=fps ...] [missing=ID:why,...]".
 * *score gets the score, or 0 when there is none. */
int score_line(const score_acc *a, int quick, int aborted, double scale, int ver, char *buf, int n, double *score);

#endif
