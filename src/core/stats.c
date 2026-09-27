/* stats.c - see stats.h. */
#include "stats.h"
#include <stdlib.h>
#include <string.h>

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

double st_percentile(const double *sorted, int n, double p)
{
    int k;
    if (n <= 0)
        return 0;
    k = (int)(p / 100.0 * n + 0.999999);        /* ceil(p/100 * n) */
    if (k < 1) k = 1;
    if (k > n) k = n;
    return sorted[k - 1];
}

void st_summarise(const double *ms, int n, st_summary *s)
{
    double *t;
    int i;
    memset(s, 0, sizeof *s);
    s->n = n;
    if (n <= 0)
        return;
    t = (double *)malloc((size_t)n * sizeof *t);
    if (!t)
        return;
    memcpy(t, ms, (size_t)n * sizeof *t);
    qsort(t, (size_t)n, sizeof *t, cmp_double);
    for (i = 0; i < n; i++)
        s->total_ms += t[i];
    s->avg_ms = s->total_ms / n;
    s->med_ms = n & 1 ? t[n / 2] : 0.5 * (t[n / 2 - 1] + t[n / 2]);
    s->p99_ms = st_percentile(t, n, 99.0);
    s->min_ms = t[0];
    s->max_ms = t[n - 1];
    s->fps = s->total_ms > 0 ? n * 1000.0 / s->total_ms : 0;
    free(t);
}

double st_median3(double a, double b, double c)
{
    if (a > b) { double x = a; a = b; b = x; }
    if (b > c) b = c;
    return a > b ? a : b;
}
