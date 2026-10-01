/* screplay.c - run a scene test on the host through rb_stub.c.
 *
 *   screplay IMPL FILE.DBS [--param N] [--size WxH] [--frames N]
 *            [--order fwd|rev|shuf] [--each] [--data DIR]
 *
 * IMPL is level, model or scene (the registry's impl). Every frame is drawn
 * once, in the order asked for; frame() must be a pure function of the
 * frame number, so each order has to print the same per-frame hashes and
 * the same total hash (make scene-stats compares them). The summary gives
 * triangles, draw calls and fill per frame: what the scene asks of a card,
 * independent of any emulator's speed. */
#include "bench.h"
#include "rb_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

db_opts db;
extern const test_impl impl_level, impl_model, impl_scene;

typedef struct {
    uint64_t hash;
    long tris, shown, draws, binds;
    double fill, fill_blend;
} fstat;

static unsigned long rng = 12345;
static unsigned long next_rand(void)
{
    rng = rng * 1103515245ul + 12345ul;
    return (rng >> 8) & 0xFFFFFF;
}

int main(int argc, char **argv)
{
    const char *impl = NULL, *file = NULL, *order = "fwd";
    int param = 0, frames = 0, each = 0, i, n, rc;
    test_def def;
    tctx t;
    fstat *st;
    int *ord;
    uint64_t total = 0xCBF29CE484222325ull;
    double sum_tris = 0, sum_shown = 0, sum_draws = 0, sum_fill = 0, sum_blend = 0, max_fill = 0, sum_binds = 0;
    long max_tris = 0, max_draws = 0;
    memset(&db, 0, sizeof db);
    db.w = 640;
    db.h = 480;
    db.data = "build/data";
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--param") && i + 1 < argc)
            param = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &db.w, &db.h);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--order") && i + 1 < argc)
            order = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc)
            db.data = argv[++i];
        else if (!strcmp(argv[i], "--each"))
            each = 1;
        else if (!impl)
            impl = argv[i];
        else if (!file)
            file = argv[i];
        else {
            fprintf(stderr, "screplay: what is %s?\n", argv[i]);
            return 2;
        }
    }
    if (!impl || !file) {
        fprintf(stderr, "usage: screplay level|model|scene FILE.DBS [--param N] [--size WxH] [--frames N] "
                        "[--order fwd|rev|shuf] [--each] [--data DIR]\n");
        return 2;
    }
    memset(&def, 0, sizeof def);
    def.id = "REPLAY";
    def.group = "scene";
    def.parent = "";
    def.title = def.what = file;
    def.file = file;
    def.param = param;
    def.metric = def.unit = "fps";
    def.impl = !strcmp(impl, "level") ? &impl_level : !strcmp(impl, "model") ? &impl_model
             : !strcmp(impl, "scene") ? &impl_scene : NULL;
    if (!def.impl) {
        fprintf(stderr, "screplay: no impl %s\n", impl);
        return 2;
    }
    rb_stub_size(db.w, db.h);
    memset(&t, 0, sizeof t);
    t.def = &def;
    rb_stub_frame();
    rc = def.impl->setup(&t);
    if (rc != 0) {
        fprintf(stderr, "screplay: setup %s (%s)\n", rc > 0 ? "skipped" : "failed", t.note);
        return 1;
    }
    n = frames ? frames : t.frames ? t.frames : 360;
    st = (fstat *)calloc((size_t)n, sizeof *st);
    ord = (int *)malloc(sizeof *ord * (size_t)n);
    if (!st || !ord)
        return 1;
    for (i = 0; i < n; i++)
        ord[i] = !strcmp(order, "rev") ? n - 1 - i : i;
    if (!strcmp(order, "shuf"))
        for (i = n - 1; i > 0; i--) {
            int j = (int)(next_rand() % (unsigned long)(i + 1)), x = ord[i];
            ord[i] = ord[j];
            ord[j] = x;
        }
    for (i = 0; i < n; i++) {
        int f = ord[i];
        rb_stub_frame();
        def.impl->frame(&t, f);
        st[f].hash = rb_stub.hash;
        st[f].tris = rb_stub.tris;
        st[f].shown = rb_stub.tris_shown;
        st[f].draws = rb_stub.draws;
        st[f].binds = rb_stub.binds;
        st[f].fill = rb_stub.fill;
        st[f].fill_blend = rb_stub.fill_blend;
    }
    for (i = 0; i < n; i++) {
        int k;
        if (each)
            printf("frame %d hash=%016llx tris=%ld shown=%ld draws=%ld binds=%ld fill=%.0f blend=%.0f\n", i,
                   (unsigned long long)st[i].hash, st[i].tris, st[i].shown, st[i].draws, st[i].binds,
                   st[i].fill, st[i].fill_blend);
        for (k = 0; k < 8; k++)
            total = (total ^ ((st[i].hash >> (8 * k)) & 0xFF)) * 0x100000001B3ull;
        sum_tris += st[i].tris;
        sum_shown += st[i].shown;
        sum_draws += st[i].draws;
        sum_binds += st[i].binds;
        sum_fill += st[i].fill;
        sum_blend += st[i].fill_blend;
        if (st[i].tris > max_tris) max_tris = st[i].tris;
        if (st[i].draws > max_draws) max_draws = st[i].draws;
        if (st[i].fill > max_fill) max_fill = st[i].fill;
    }
    printf("%s %s param=%d %dx%d frames=%d order=%s tris=%.0f/%ld shown=%.0f draws=%.0f/%ld binds=%.0f "
           "fill=%.2f/%.2f blend=%.2f tex_kb=%lu hash=%016llx note=%s\n",
           impl, file, param, db.w, db.h, n, order, sum_tris / n, max_tris, sum_shown / n, sum_draws / n, max_draws,
           sum_binds / n, sum_fill / n / (db.w * db.h), max_fill / (db.w * db.h), sum_blend / n / (db.w * db.h),
           rb_tex_bytes() / 1024, (unsigned long long)total, t.note);
    def.impl->done(&t);
    free(st);
    free(ord);
    return 0;
}
