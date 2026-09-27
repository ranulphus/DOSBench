/* results.c - measurement records: C:\OUT\RESULTS.TXT, one line per run
 * header and per test, key=value separated by spaces (values never contain
 * spaces), appended so several programs can share the file. Each line is
 * also sent over COM1 as "HX-STAT bench ...". */
#include "bench.h"
#include "mga/serial.h"
#include <stdarg.h>
#include <stdio.h>

static FILE *rf;

int res_open(const char *dir)
{
    char path[128];
    snprintf(path, sizeof path, "%s\\RESULTS.TXT", dir);
    rf = fopen(path, "a");
    return rf ? 0 : -1;
}

void res_close(void)
{
    if (rf)
        fclose(rf);
    rf = NULL;
}

void res_line(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (rf) {
        fputs(buf, rf);
        fputc('\n', rf);
        fflush(rf);
    }
    serial_puts("HX-STAT bench ");      /* hx_stat truncates at 300 characters */
    serial_puts(buf);
    serial_puts("\n");
}
