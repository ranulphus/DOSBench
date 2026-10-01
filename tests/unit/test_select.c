/* test_select.c - the programs' test selection (select.c over the real
 * registry) against tests/fixtures/select.txt, which tools/registry.py
 * selftest checks too. */
#include "unit.h"
#include "bench.h"
#include <string.h>

/* The tests' code is not needed: stub every impl the registry names. */
#define DB_IMPL(name) const test_impl impl_##name = { 0, 0, 0 };
#include "registry.h"

void unit_run(void)
{
    FILE *f = fopen("tests/fixtures/select.txt", "r");
    char line[2048];
    int cases = 0;
    CHECK(f != NULL);
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *arrow = strstr(line, "->"), got[2048] = "", want[2048] = "", *w;
        const test_def *d;
        size_t k = 0;
        if (line[0] == '#' || !arrow)
            continue;
        *arrow = 0;
        for (d = db_tests; d->id; d++)
            if (reg_selected(d, line))
                k += (size_t)snprintf(got + k, sizeof got - k, "%s%s", k ? " " : "", d->id);
        k = 0;
        for (w = strtok(arrow + 2, " \t\r\n"); w; w = strtok(NULL, " \t\r\n"))
            k += (size_t)snprintf(want + k, sizeof want - k, "%s%s", k ? " " : "", w);
        if (strcmp(got, want))
            fprintf(stderr, "select '%s': %s\n  want %s\n", line, got, want);
        CHECK(!strcmp(got, want));
        cases++;
    }
    fclose(f);
    CHECK(cases >= 10);
    CHECK(reg_count("FILL") == 6);
    {
        const test_def *d;
        for (d = db_tests; d->id; d++)
            if (!strcmp(d->id, "FILL"))
                CHECK(reg_suite_used(d, "S1TEXB") && !reg_suite_used(d, "S2M16"));
    }
}
