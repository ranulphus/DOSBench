/* test_timer.c - calibration arithmetic and the host clock. */
#include "unit.h"
#include "timer.h"

void unit_run(void)
{
    tmr_t a, b;
    /* A 400 MHz TSC over 6 BIOS ticks (6 * 65536 / 1193182 s). */
    double secs = 6 * 65536.0 / 1193182.0;
    CHECK_NEAR(tmr_hz_from((tmr_t)(400e6 * secs), 6), 400e6, 10.0);   /* the count is truncated to an integer */
    CHECK(tmr_hz_from(1000, 0) == 0);
    CHECK(tmr_init() == 0);
    a = tmr_now();
    b = tmr_now();
    CHECK(b >= a);
    CHECK_NEAR(tmr_ms(1000000), 1.0, 1e-12);   /* host clock counts nanoseconds */
}
