/* timer.c - see timer.h. */
#include "timer.h"
#include "stats.h"

static double hz = TMR_BIOS_HZ;
static const char *source = "bios";

double tmr_hz_from(tmr_t counts, int ticks)
{
    return ticks > 0 ? (double)counts * TMR_BIOS_HZ / ticks : 0.0;
}

double tmr_hz(void) { return hz; }
const char *tmr_source(void) { return source; }
double tmr_ms(tmr_t dt) { return (double)dt * 1000.0 / hz; }

#if defined(__WATCOMC__) || defined(__DJGPP__)
#include "mga/sys.h"

static int use_tsc;

static uint32_t bios_ticks(void)
{
    return *(volatile uint32_t *)sys_real_ptr(0x40, 0x6C);
}

#if defined(__WATCOMC__)
/* Hand-assembled so no assembler directive or CPU level is needed. */
static uint32_t eflags_id_toggles(void);
#pragma aux eflags_id_toggles = \
    0x9C                        /* pushfd */            \
    0x58                        /* pop eax */           \
    0x89 0xC1                   /* mov ecx, eax */      \
    0x35 0x00 0x00 0x20 0x00    /* xor eax, 200000h */  \
    0x50                        /* push eax */          \
    0x9D                        /* popfd */             \
    0x9C                        /* pushfd */            \
    0x58                        /* pop eax */           \
    0x51                        /* push ecx */          \
    0x9D                        /* popfd */             \
    0x31 0xC8                   /* xor eax, ecx */      \
    value [eax] modify exact [eax ecx];
static uint32_t cpuid1_edx(void);
#pragma aux cpuid1_edx = \
    0xB8 0x01 0x00 0x00 0x00    /* mov eax, 1 */        \
    0x0F 0xA2                   /* cpuid */             \
    value [edx] modify exact [eax ebx ecx edx];
static void tsc_read(uint32_t *p);
#pragma aux tsc_read = \
    0x0F 0x31                   /* rdtsc */             \
    0x89 0x03                   /* mov [ebx], eax */    \
    0x89 0x53 0x04              /* mov [ebx+4], edx */  \
    parm [ebx] modify exact [eax edx];

static int have_tsc(void)
{
    return (eflags_id_toggles() & 0x200000u) && (cpuid1_edx() & 0x10u);
}

static tmr_t tsc(void)
{
    uint32_t v[2];
    tsc_read(v);
    return ((tmr_t)v[1] << 32) | v[0];
}
#else
#include <cpuid.h>
#include <time.h>

static int have_tsc(void)
{
    unsigned a, b, c, d;
    return __get_cpuid(1, &a, &b, &c, &d) && (d & 0x10u);
}

static tmr_t tsc(void)
{
    uint64_t v;
    __asm__ __volatile__("rdtsc" : "=A"(v));
    return v;
}
#endif

tmr_t tmr_now(void)
{
    if (use_tsc)
        return tsc();
#if defined(__DJGPP__)
    return (tmr_t)uclock();
#else
    return bios_ticks();
#endif
}

int tmr_init(void)
{
    double w[3];
    int i;
    if (!have_tsc()) {
#if defined(__DJGPP__)
        hz = UCLOCKS_PER_SEC;
        source = "uclock";
        uclock();
#endif
        return 0;
    }
    for (i = 0; i < 3; i++) {
        uint32_t t0 = bios_ticks();
        tmr_t c0;
        long spin = 0;
        while (bios_ticks() == t0)              /* align to a tick edge */
            if (++spin > 200000000L)
                return -1;                      /* the tick is not running */
        t0 = bios_ticks();
        c0 = tsc();
        while (bios_ticks() - t0 < TMR_CAL_TICKS)
            ;
        w[i] = tmr_hz_from(tsc() - c0, TMR_CAL_TICKS);
    }
    hz = st_median3(w[0], w[1], w[2]);
    use_tsc = 1;
    source = "tsc";
    return 0;
}

#else
/* Host builds (unit tests): a monotonic clock. */
#include <time.h>

tmr_t tmr_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (tmr_t)ts.tv_sec * 1000000000u + (tmr_t)ts.tv_nsec;
}

int tmr_init(void)
{
    hz = 1e9;
    source = "host";
    return 0;
}
#endif
