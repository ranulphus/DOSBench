/* timer.h - high-resolution timing for DOS.
 *
 * The Pentium's time-stamp counter, calibrated at start-up against the BIOS
 * tick (the PIT at 1193182/65536 Hz): three windows of TMR_CAL_TICKS ticks,
 * median taken. Without a TSC (a 486) the BIOS tick itself is used (55 ms
 * resolution; DJGPP falls back to uclock() instead). */
#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

#define TMR_BIOS_HZ   (1193182.0 / 65536.0)
#define TMR_CAL_TICKS 6                  /* ~0.33 s per calibration window */

typedef uint64_t tmr_t;

int         tmr_init(void);              /* 0 on success */
tmr_t       tmr_now(void);
double      tmr_hz(void);                /* counter frequency */
double      tmr_ms(tmr_t dt);            /* counter delta to milliseconds */
const char *tmr_source(void);            /* "tsc", "uclock" or "bios" */
/* Counter frequency from a calibration window: counts over ticks BIOS ticks. */
double      tmr_hz_from(tmr_t counts, int ticks);

#endif
