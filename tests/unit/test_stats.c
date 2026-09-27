/* test_stats.c - frame-time statistics. */
#include "unit.h"
#include "stats.h"

void unit_run(void)
{
    double ms[100], sorted[5] = { 1, 2, 3, 4, 5 };
    st_summary s;
    int i;
    for (i = 0; i < 100; i++)
        ms[i] = 10.0;
    ms[37] = 50.0;                          /* one hitch */
    st_summarise(ms, 100, &s);
    CHECK(s.n == 100);
    CHECK_NEAR(s.total_ms, 1040.0, 1e-9);
    CHECK_NEAR(s.avg_ms, 10.4, 1e-9);
    CHECK_NEAR(s.med_ms, 10.0, 1e-9);
    CHECK_NEAR(s.p99_ms, 10.0, 1e-9);       /* nearest rank: the 99th of 100 */
    CHECK_NEAR(s.max_ms, 50.0, 1e-9);
    CHECK_NEAR(s.fps, 100 * 1000.0 / 1040.0, 1e-9);
    ms[12] = 40.0;                          /* two hitches: the 99th is the smaller */
    st_summarise(ms, 100, &s);
    CHECK_NEAR(s.p99_ms, 40.0, 1e-9);
    CHECK(ms[12] == 40.0 && ms[37] == 50.0); /* input untouched */
    /* Even count: mean of the middle two. */
    ms[0] = 1; ms[1] = 2; ms[2] = 3; ms[3] = 10;
    st_summarise(ms, 4, &s);
    CHECK_NEAR(s.med_ms, 2.5, 1e-9);
    CHECK_NEAR(st_percentile(sorted, 5, 50), 3, 1e-9);
    CHECK_NEAR(st_percentile(sorted, 5, 100), 5, 1e-9);
    CHECK_NEAR(st_percentile(sorted, 5, 1), 1, 1e-9);
    CHECK_NEAR(st_median3(3, 1, 2), 2, 0);
    CHECK_NEAR(st_median3(1, 3, 2), 2, 0);
    CHECK_NEAR(st_median3(2, 2, 9), 2, 0);
    st_summarise(ms, 0, &s);
    CHECK(s.n == 0 && s.fps == 0);
}
