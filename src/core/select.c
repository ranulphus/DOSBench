/* select.c - which tests a --tests list runs (no rendering; host-tested
 * against tests/fixtures/select.txt, as tools/registry.py is).
 *
 * A list holds names separated by commas or spaces: all, a preset (full,
 * scenes, features), a group, a suite (its phases), a test id, or an id
 * prefix of up to 3 characters. The order of the list does not matter:
 * tests run in registry order. */
#include "bench.h"
#include <string.h>

static int preset_selects(const test_def *d, const char *name, size_t n, int depth);

/* Does the token tok[0..n) select test d? */
static int token_selects(const test_def *d, const char *tok, size_t n, int depth)
{
#define IS(s) (strlen(s) == n && !strncmp(tok, s, n))
    if (IS("all") || IS(d->group) || IS(d->id) || (d->parent[0] && IS(d->parent)))
        return 1;
    if (n <= 3 && !strncmp(tok, d->id, n))
        return 1;
    return preset_selects(d, tok, n, depth);
#undef IS
}

static int list_selects(const test_def *d, const char *list, int depth)
{
    const char *p = list;
    if (depth > 2)                      /* presets name groups, never each other */
        return 0;
    while (*p) {
        size_t n;
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        n = strcspn(p, ", \t\r\n");
        if (n && token_selects(d, p, n, depth))
            return 1;
        p += n;
    }
    return 0;
}

static int preset_selects(const test_def *d, const char *name, size_t n, int depth)
{
    const db_preset *ps;
    for (ps = db_presets; ps->id; ps++)
        if (strlen(ps->id) == n && !strncmp(name, ps->id, n))
            return list_selects(d, ps->tests, depth + 1);
    return 0;
}

int reg_selected(const test_def *d, const char *list)
{
    if (d->flags & T_SUITE)
        return 0;
    return list_selects(d, list, 0);
}

int reg_suite_used(const test_def *suite, const char *list)
{
    const test_def *d;
    for (d = db_tests; d->id; d++)
        if (!strcmp(d->parent, suite->id) && reg_selected(d, list))
            return 1;
    return 0;
}

int reg_count(const char *list)
{
    const test_def *d;
    int n = 0;
    for (d = db_tests; d->id; d++)
        n += reg_selected(d, list);
    return n;
}
