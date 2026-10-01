/* test_vmath.c - matrices against hand-computed values. */
#include "unit.h"
#include "vmath.h"

void unit_run(void)
{
    mat4 p, v, m;
    float o[4], pt[3] = { 1, 2, 3 };
    float eye[3] = { 0, 0, 5 }, at[3] = { 0, 0, 0 }, up[3] = { 0, 1, 0 };
    float a[3] = { 1, 0, 0 }, b[3] = { 0, 1, 0 }, c[3];
    m4_identity(&m);
    m4_xform(&m, pt, o);
    CHECK(o[0] == 1 && o[1] == 2 && o[2] == 3 && o[3] == 1);
    m4_translate(&m, 10, 0, 0);
    m4_scale(&m, 2, 2, 2);                  /* T * S: scale first */
    m4_xform(&m, pt, o);
    CHECK_NEAR(o[0], 12, 1e-6);
    CHECK_NEAR(o[1], 4, 1e-6);
    /* Perspective: a point on the near plane maps to z/w = -1, far to +1. */
    m4_perspective(&p, 90, 1, 1, 100);
    pt[0] = 1; pt[1] = 0; pt[2] = -1;
    m4_xform(&p, pt, o);
    CHECK_NEAR(o[2] / o[3], -1, 1e-5);
    CHECK_NEAR(o[0] / o[3], 1, 1e-5);       /* 90 degrees: x = z at the edge */
    CHECK_NEAR(o[3], 1, 1e-6);              /* w = eye distance */
    pt[2] = -100;
    m4_xform(&p, pt, o);
    CHECK_NEAR(o[2] / o[3], 1, 1e-4);
    /* Look-at from +z: the origin lands 5 units in front. */
    m4_lookat(&v, eye, at, up);
    pt[0] = pt[1] = pt[2] = 0;
    m4_xform(&v, pt, o);
    CHECK_NEAR(o[2], -5, 1e-6);
    CHECK_NEAR(o[0], 0, 1e-6);
    /* Rotation by 90 about y takes +x to -z. */
    m4_identity(&m);
    m4_rotate_y(&m, 90);
    pt[0] = 1; pt[1] = 0; pt[2] = 0;
    m4_xform(&m, pt, o);
    CHECK_NEAR(o[2], -1, 1e-6);
    CHECK_NEAR(o[0], 0, 1e-6);
    /* Multiplication order: (P * V) x = P (V x). */
    m4_mul(&m, &p, &v);
    pt[0] = 0.5f; pt[1] = -0.25f; pt[2] = 1;
    {
        float t[4], o2[4];
        m4_xform(&v, pt, t);
        m4_xform(&p, t, o2);
        m4_xform(&m, pt, o);
        CHECK_NEAR(o[0], o2[0], 1e-5);
        CHECK_NEAR(o[3], o2[3], 1e-5);
    }
    v3_cross(c, a, b);
    CHECK(c[0] == 0 && c[1] == 0 && c[2] == 1);
    /* Catmull-Rom passes through its middle points. */
    {
        float p0[3] = { 0, 0, 0 }, p1[3] = { 1, 0, 0 }, p2[3] = { 2, 1, 0 }, p3[3] = { 3, 1, 0 };
        v3_catmull(c, p0, p1, p2, p3, 0);
        CHECK_NEAR(c[0], 1, 1e-6);
        v3_catmull(c, p0, p1, p2, p3, 1);
        CHECK_NEAR(c[0], 2, 1e-6);
        CHECK_NEAR(c[1], 1, 1e-6);
    }
    /* Rotation about an axis equals the fixed-axis ones; yaw, pitch and roll compose in that order. */
    {
        float ax[3] = { 1, 0, 0 }, ay[3] = { 0, 1, 0 }, az[3] = { 0, 0, 1 };
        mat4 r1, r2;
        int k, j;
        const float *axes[3] = { ax, ay, az };
        for (j = 0; j < 3; j++) {
            m4_identity(&r1);
            m4_identity(&r2);
            m4_rotate_axis(&r1, axes[j], 37);
            if (j == 0) m4_rotate_x(&r2, 37);
            if (j == 1) m4_rotate_y(&r2, 37);
            if (j == 2) m4_rotate_z(&r2, 37);
            for (k = 0; k < 16; k++)
                CHECK_NEAR(r1.m[k], r2.m[k], 1e-6);
        }
        m4_identity(&r1);
        m4_ypr(&r1, 90, 0, 0);                  /* models face +z: yaw 90 faces +x */
        CHECK_NEAR(r1.m[8], 1, 1e-6);
        m4_identity(&r1);
        m4_ypr(&r1, 0, 90, 0);                  /* pitch 90 faces -y */
        CHECK_NEAR(r1.m[9], -1, 1e-6);
    }
    /* A frame: +z along fwd, +y towards up, scaled, placed; m4_rot_inv undoes the rotation. */
    {
        float pos[3] = { 1, 2, 3 }, fwd[3] = { 0, 0, -2 }, up[3] = { 0, 1, 0.2f }, v[3] = { 0.3f, -0.4f, 0.5f }, o[4], back[3];
        mat4 f;
        m4_from_frame(&f, pos, fwd, up, 2);
        CHECK_NEAR(f.m[10], -2, 1e-6);          /* z column: fwd, length 2 */
        CHECK_NEAR(f.m[5], 2, 1e-5);
        CHECK_NEAR(f.m[12], 1, 1e-6);
        CHECK_NEAR(f.m[0], -2, 1e-6);           /* x = up x fwd: left of the way it faces */
        m4_identity(&f);
        m4_rotate_axis(&f, up, 71);
        m4_xform(&f, v, o);
        m4_rot_inv(&f, o, back);
        CHECK_NEAR(back[0], v[0], 1e-5);
        CHECK_NEAR(back[1], v[1], 1e-5);
        CHECK_NEAR(back[2], v[2], 1e-5);
    }
}
