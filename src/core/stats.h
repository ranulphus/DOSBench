/* stats.h - frame-time statistics: average, median and 99th percentile
 * ("1% low") frame times, as modern frame-time reporting does. */
#ifndef STATS_H
#define STATS_H

typedef struct {
    int    n;
    double total_ms;
    double avg_ms, med_ms, p99_ms, min_ms, max_ms;
    double fps;                 /* n / total */
} st_summary;

/* Summarise n frame times in milliseconds (the array is not modified). */
void st_summarise(const double *ms, int n, st_summary *out);
/* Nearest-rank percentile (0 < p <= 100) of a sorted array. */
double st_percentile(const double *sorted, int n, double p);
/* Median of three (timer calibration). */
double st_median3(double a, double b, double c);

#endif
