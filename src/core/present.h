/* present.h - what the screen shows between measurements (present.c). */
#ifndef PRESENT_H
#define PRESENT_H
#include "bench.h"

/* A finished test, as the cards and captions show it. */
typedef struct {
    const test_def *d;              /* NULL: none yet */
    char status[8];                 /* ok, skip, fail */
    double value;                   /* its metric (fps or a rate) */
    double fps, p99_ms;
} db_result;

/* Each returns 1 when Esc was pressed (stop after the current test). */
int pr_title(const test_def *d, int k, int n, const db_result *prev);
int pr_caption(const test_def *d, tctx *t, int i, int ni, int k, int n, const db_result *prev);
int pr_summary(const test_def *suite, const db_result *r, int nr, int k, int n);

#endif
