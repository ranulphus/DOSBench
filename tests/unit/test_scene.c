/* test_scene.c - .DBS parsing against tools/dbs.py's fixture, and damage. */
#include "unit.h"
#include "scene.h"
#include <stdlib.h>
#include <string.h>

/* Backend stubs: parsing needs none of them. */
rb_tex *rb_tex_create(int w, int h, const uint8_t *rgba, int flags) { (void)w; (void)h; (void)rgba; (void)flags; return NULL; }
void rb_tex_free(rb_tex *t) { (void)t; }
rb_mesh *rb_mesh_create(const rb_vertex *v, int nv, const uint16_t *i, int ni) { (void)v; (void)nv; (void)i; (void)ni; return NULL; }
void rb_mesh_free(rb_mesh *m) { (void)m; }

static long slurp(const char *path, uint8_t **buf)
{
    FILE *f = fopen(path, "rb");
    long n;
    *buf = NULL;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *buf = (uint8_t *)malloc((size_t)n);
    if (fread(*buf, 1, (size_t)n, f) != (size_t)n) n = -1;
    fclose(f);
    return n;
}

void unit_run(void)
{
    scene s;
    char err[96], buf[32];
    float eye[3], at[3];
    uint8_t *raw = NULL, *copy;
    long n = slurp("build/host/selftest.dbs", &raw);
    CHECK(n > 0);
    CHECK(sc_load(&s, "build/host/selftest.dbs", err, sizeof err) == 0);
    CHECK(s.ntex == 1 && s.tex[0].w == 4 && s.tex[0].h == 2 && s.tex[0].flags == (SC_TF_MIPMAP | SC_TF_CLAMP));
    CHECK(s.tex[0].rgba[0] == 0 && s.tex[0].rgba[31] == 31);
    CHECK(s.nvert == 7 && s.nidx == 9 && s.nbatch == 2);
    CHECK(s.batch[0].tex == 0 && s.batch[0].lm == SC_NONE && s.batch[0].icount == 3);
    CHECK(s.batch[1].vfirst == 3 && s.batch[1].vcount == 4 && s.batch[1].ifirst == 3 && s.batch[1].flags == SC_BF_TWOSIDED);
    CHECK_NEAR(s.batch[1].mins[2], -2, 0);
    CHECK(s.vert[1].c[1] == 255 && s.vert[2].c[3] == 128);
    CHECK_NEAR(s.vert[6].z, -2, 0);
    CHECK_NEAR(s.vert[1].u2, 1, 0);
    CHECK(s.idx[7] == 3);
    CHECK_NEAR(s.tris, 3, 0);
    CHECK(s.view.kind == 0 && s.view.frames == 4);
    CHECK_NEAR(s.view.zfar, 50, 0);
    sc_camera(&s, 1, eye, at);                  /* a quarter turn: +x side */
    CHECK_NEAR(eye[0], 11, 1e-5);
    CHECK_NEAR(eye[1], 7, 1e-5);
    CHECK_NEAR(eye[2], 3, 1e-5);
    CHECK_NEAR(at[1], 2, 0);
    CHECK(!strcmp(sc_info(&s, "name", buf, sizeof buf), "SELFTEST"));
    CHECK(!strcmp(sc_info(&s, "licence", buf, sizeof buf), "CC0-1.0"));
    CHECK(!strcmp(sc_info(&s, "nothing", buf, sizeof buf), ""));
    sc_free(&s);
    /* Truncations anywhere are rejected, never read past the end. */
    {
        long cut;
        int rejected = 0;
        for (cut = 0; cut < n; cut += 7) {
            copy = (uint8_t *)malloc((size_t)(cut ? cut : 1));
            memcpy(copy, raw, (size_t)cut);
            if (sc_parse(&s, copy, cut, err, sizeof err) < 0)
                rejected++;
            sc_free(&s);
        }
        CHECK(rejected == (n + 6) / 7);
    }
    /* A batch that points past the vertices. */
    copy = (uint8_t *)malloc((size_t)n);
    memcpy(copy, raw, (size_t)n);
    {
        long i;
        for (i = 16; i + 4 < n; i++)
            if (!memcmp(copy + i, "BTCH", 4)) {
                copy[i + 8 + 4 + 8] = 200;      /* batch 0 vfirst */
                break;
            }
    }
    CHECK(sc_parse(&s, copy, n, err, sizeof err) < 0);
    CHECK(strstr(err, "batch 0") != NULL);
    sc_free(&s);
    free(raw);
}
