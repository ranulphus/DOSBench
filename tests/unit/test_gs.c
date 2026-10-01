/* test_gs.c - the game-scene runtime against tools/dbs.py's selftest2 scene. */
#include "unit.h"
#include "gs.h"
#include "rb_stub.h"
#include <math.h>
#include <string.h>

db_opts db;

static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

void unit_run(void)
{
    scene s;
    gs_world w;
    char err[96];
    const char *why = "";
    float pos[3], fwd[3], up[3], eye[3], at[3], fov, a, b;
    mat4 m, car;
    uint64_t h1, h2;
    int i, ok;
    db.w = 640;
    db.h = 480;
    CHECK(sc_load(&s, "build/host/selftest2.dbs", err, sizeof err) == 0);
    CHECK(s.version == 2 && s.ghdr && s.ghdr->frames == 100 && s.ghdr->capture == 50);
    CHECK(s.ninst == 14 && s.npart == 3 && s.nemit == 2 && s.nfxev == 2 && s.ncam == 5 && s.nsurf == 4);
    CHECK(s.ntrack == 1 && s.nanim == 1 && s.nnormal == 256 && s.vnormal);

    /* The track: a closed circle of radius 20 at height 1, banked 10 degrees. */
    gs_track_at(&s.track[0], 0, pos, fwd, up);
    CHECK(near(pos[0], 0, 1e-3f) && near(pos[1], 1, 1e-3f) && near(pos[2], 20, 1e-3f));
    CHECK(fwd[0] > 0.99f);                                  /* going +x at the top of the circle */
    CHECK(near(up[1], (float)cos(10 * VM_PI / 180), 1e-3f) && near(up[2], -(float)sin(10 * VM_PI / 180), 1e-3f));
    gs_track_at(&s.track[0], s.track[0].length, eye, fwd, up);   /* closed: the end is the start */
    CHECK(near(eye[0], pos[0], 1e-3f) && near(eye[2], pos[2], 1e-3f));
    gs_track_at(&s.track[0], -s.track[0].length / 4, eye, fwd, up);
    CHECK(near(eye[0], -20, 0.2f) && near(eye[2], 0, 0.2f));     /* a quarter back round */

    /* Instances. */
    CHECK(gs_inst_at(&s, 0, 0, &m));
    CHECK(near(m.m[12], 0, 1e-5f) && near(m.m[13], 1, 1e-5f) && near(m.m[14], 0, 1e-5f));
    CHECK(near(m.m[8], 0.5f, 1e-4f) && near(m.m[10], 0.8660254f, 1e-4f));   /* yaw 30: +z turns to +x */
    CHECK(!gs_inst_at(&s, 10, 9 / 25.0, &m) && gs_inst_at(&s, 10, 10 / 25.0, &m) && !gs_inst_at(&s, 10, 80 / 25.0, &m));
    CHECK(gs_inst_at(&s, 1, 2.0, &car) && gs_inst_at(&s, 2, 2.0, &m));      /* the child rides on its parent */
    CHECK(near(m.m[12], car.m[12] + 2 * car.m[4], 1e-3f) && near(m.m[13], car.m[13] + 2 * car.m[5], 1e-3f));
    a = car.m[0] * car.m[0] + car.m[1] * car.m[1] + car.m[2] * car.m[2];
    CHECK(near(a, 1, 1e-3f));                               /* track instances keep their scale */
    CHECK(gs_inst_at(&s, 3, 1.0, &m));                      /* orbit: radius 12 about (0, 6, 0), tilted 20 */
    a = m.m[12] * m.m[12] + (m.m[13] - 6) * (m.m[13] - 6) + m.m[14] * m.m[14];
    CHECK(near(a, 144, 0.05f));

    /* Cameras: a shot of each kind. */
    gs_camera(&s, 0, eye, at, up, &fov);
    CHECK(near(eye[0], 0, 1e-3f) && near(eye[1], 4, 1e-3f) && near(eye[2], 20, 1e-3f) && near(fov, 60, 1e-6f));
    gs_camera(&s, 45, eye, at, up, &fov);
    CHECK(near(eye[0], 25, 1e-5f) && near(fov, 45, 1e-6f));
    gs_camera(&s, 85, eye, at, up, &fov);
    CHECK(near(eye[1], 12, 1e-4f) && near(at[1], 1, 1e-4f));
    gs_camera(&s, 25, eye, at, up, &fov);                   /* chase: behind and above the car */
    gs_inst_at(&s, 1, 25 / 25.0, &car);
    CHECK(eye[1] > car.m[13] + 2.5f);

    /* Random numbers: in range, repeatable, different per channel. */
    a = gs_rand(1, 2, 3);
    b = gs_rand(1, 2, 4);
    CHECK(a >= 0 && a < 1 && a == gs_rand(1, 2, 3) && a != b);

    /* Frames: pure functions of the frame number. */
    CHECK(sc_upload(&s, 1) == 0);
    CHECK(gs_init(&w, &s, &why) == 0);
    CHECK(w.pmax > 0);
    rb_stub_frame();
    gs_frame(&w, 45);
    h1 = rb_stub.hash;
    for (i = 0; i < 100; i += 7) {
        rb_stub_frame();
        gs_frame(&w, i);
    }
    rb_stub_frame();
    gs_frame(&w, 45);
    h2 = rb_stub.hash;
    CHECK(h1 == h2);
    CHECK(w.np > 0);                                        /* frame 45: smoke and sparks */
    gs_frame(&w, 85);
    ok = w.drawn[5] != 0;                                   /* the far cube: its LOD, or culled */
    CHECK(ok);
    CHECK(w.drawn[0] == 0 || w.drawn[0] == SC_NONE);
    gs_free(&w);
    sc_free(&s);
}
