/* vmath.c - see vmath.h. */
#include "vmath.h"
#include <math.h>
#include <string.h>

#define M(c, r) m[(c) * 4 + (r)]

void m4_identity(mat4 *mt)
{
    float *m = mt->m;
    memset(m, 0, sizeof mt->m);
    M(0, 0) = M(1, 1) = M(2, 2) = M(3, 3) = 1.0f;
}

void m4_mul(mat4 *r, const mat4 *a, const mat4 *b)
{
    mat4 t;
    int c, k;
    for (c = 0; c < 4; c++)
        for (k = 0; k < 4; k++)
            t.m[c * 4 + k] = a->m[0 * 4 + k] * b->m[c * 4 + 0] + a->m[1 * 4 + k] * b->m[c * 4 + 1] +
                             a->m[2 * 4 + k] * b->m[c * 4 + 2] + a->m[3 * 4 + k] * b->m[c * 4 + 3];
    *r = t;
}

void m4_frustum(mat4 *mt, float l, float r, float b, float t, float n, float f)
{
    float *m = mt->m;
    memset(m, 0, sizeof mt->m);
    M(0, 0) = 2 * n / (r - l);
    M(1, 1) = 2 * n / (t - b);
    M(2, 0) = (r + l) / (r - l);
    M(2, 1) = (t + b) / (t - b);
    M(2, 2) = -(f + n) / (f - n);
    M(2, 3) = -1.0f;
    M(3, 2) = -2 * f * n / (f - n);
}

void m4_perspective(mat4 *mt, float fovy_deg, float aspect, float znear, float zfar)
{
    float t = znear * (float)tan(fovy_deg * VM_PI / 360.0);
    m4_frustum(mt, -t * aspect, t * aspect, -t, t, znear, zfar);
}

void m4_lookat(mat4 *mt, const float eye[3], const float at[3], const float up[3])
{
    float f[3], s[3], u[3];
    float *m = mt->m;
    v3_sub(f, at, eye);
    v3_norm(f);
    v3_cross(s, f, up);
    v3_norm(s);
    v3_cross(u, s, f);
    m4_identity(mt);
    M(0, 0) = s[0]; M(1, 0) = s[1]; M(2, 0) = s[2];
    M(0, 1) = u[0]; M(1, 1) = u[1]; M(2, 1) = u[2];
    M(0, 2) = -f[0]; M(1, 2) = -f[1]; M(2, 2) = -f[2];
    M(3, 0) = -v3_dot(s, eye);
    M(3, 1) = -v3_dot(u, eye);
    M(3, 2) = v3_dot(f, eye);
}

void m4_translate(mat4 *mt, float x, float y, float z)
{
    mat4 t;
    m4_identity(&t);
    t.m[12] = x; t.m[13] = y; t.m[14] = z;
    m4_mul(mt, mt, &t);
}

void m4_scale(mat4 *mt, float x, float y, float z)
{
    mat4 t;
    m4_identity(&t);
    t.m[0] = x; t.m[5] = y; t.m[10] = z;
    m4_mul(mt, mt, &t);
}

void m4_rotate_y(mat4 *mt, float deg)
{
    mat4 t;
    float c = (float)cos(deg * VM_PI / 180.0), s = (float)sin(deg * VM_PI / 180.0);
    m4_identity(&t);
    t.m[0] = c; t.m[2] = -s; t.m[8] = s; t.m[10] = c;
    m4_mul(mt, mt, &t);
}

void m4_rotate_x(mat4 *mt, float deg)
{
    mat4 t;
    float c = (float)cos(deg * VM_PI / 180.0), s = (float)sin(deg * VM_PI / 180.0);
    m4_identity(&t);
    t.m[5] = c; t.m[6] = s; t.m[9] = -s; t.m[10] = c;
    m4_mul(mt, mt, &t);
}

void m4_rotate_z(mat4 *mt, float deg)
{
    mat4 t;
    float c = (float)cos(deg * VM_PI / 180.0), s = (float)sin(deg * VM_PI / 180.0);
    m4_identity(&t);
    t.m[0] = c; t.m[1] = s; t.m[4] = -s; t.m[5] = c;
    m4_mul(mt, mt, &t);
}

void m4_rotate_axis(mat4 *mt, const float axis[3], float deg)
{
    mat4 t;
    float a[3], c = (float)cos(deg * VM_PI / 180.0), s = (float)sin(deg * VM_PI / 180.0), k = 1 - c;
    float *m = t.m;
    a[0] = axis[0]; a[1] = axis[1]; a[2] = axis[2];
    v3_norm(a);
    m4_identity(&t);
    M(0, 0) = a[0] * a[0] * k + c;        M(1, 0) = a[0] * a[1] * k - a[2] * s; M(2, 0) = a[0] * a[2] * k + a[1] * s;
    M(0, 1) = a[1] * a[0] * k + a[2] * s; M(1, 1) = a[1] * a[1] * k + c;        M(2, 1) = a[1] * a[2] * k - a[0] * s;
    M(0, 2) = a[2] * a[0] * k - a[1] * s; M(1, 2) = a[2] * a[1] * k + a[0] * s; M(2, 2) = a[2] * a[2] * k + c;
    m4_mul(mt, mt, &t);
}

void m4_ypr(mat4 *mt, float yaw, float pitch, float roll)
{
    if (yaw != 0)
        m4_rotate_y(mt, yaw);
    if (pitch != 0)
        m4_rotate_x(mt, pitch);
    if (roll != 0)
        m4_rotate_z(mt, roll);
}

void m4_from_frame(mat4 *mt, const float pos[3], const float fwd[3], const float up[3], float s)
{
    float x[3], y[3], z[3];
    float *m = mt->m;
    z[0] = fwd[0]; z[1] = fwd[1]; z[2] = fwd[2];
    v3_norm(z);
    v3_cross(x, up, z);
    v3_norm(x);
    v3_cross(y, z, x);
    m4_identity(mt);
    M(0, 0) = x[0] * s; M(0, 1) = x[1] * s; M(0, 2) = x[2] * s;
    M(1, 0) = y[0] * s; M(1, 1) = y[1] * s; M(1, 2) = y[2] * s;
    M(2, 0) = z[0] * s; M(2, 1) = z[1] * s; M(2, 2) = z[2] * s;
    M(3, 0) = pos[0]; M(3, 1) = pos[1]; M(3, 2) = pos[2];
}

void m4_rigid_inverse(mat4 *r, const mat4 *mt)
{
    const float *m = mt->m;
    mat4 t;
    int c, k;
    m4_identity(&t);
    for (c = 0; c < 3; c++)
        for (k = 0; k < 3; k++)
            t.m[c * 4 + k] = m[k * 4 + c];
    for (k = 0; k < 3; k++)
        t.m[12 + k] = -(t.m[k] * m[12] + t.m[4 + k] * m[13] + t.m[8 + k] * m[14]);
    *r = t;
}

void m4_rot_inv(const mat4 *mt, const float v[3], float out[3])
{
    const float *m = mt->m;
    float r[3];
    int c;
    for (c = 0; c < 3; c++)
        r[c] = M(c, 0) * v[0] + M(c, 1) * v[1] + M(c, 2) * v[2];
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
}

void m4_xform(const mat4 *mt, const float v[3], float out[4])
{
    const float *m = mt->m;
    int k;
    for (k = 0; k < 4; k++)
        out[k] = M(0, k) * v[0] + M(1, k) * v[1] + M(2, k) * v[2] + M(3, k);
}

void v3_sub(float r[3], const float a[3], const float b[3])
{
    r[0] = a[0] - b[0]; r[1] = a[1] - b[1]; r[2] = a[2] - b[2];
}

void v3_cross(float r[3], const float a[3], const float b[3])
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    r[0] = x; r[1] = y; r[2] = z;
}

float v3_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void v3_norm(float v[3])
{
    float l = (float)sqrt(v3_dot(v, v));
    if (l > 0) {
        v[0] /= l; v[1] /= l; v[2] /= l;
    }
}

void v3_catmull(float r[3], const float p0[3], const float p1[3], const float p2[3], const float p3[3], float t)
{
    float t2 = t * t, t3 = t2 * t;
    int k;
    for (k = 0; k < 3; k++)
        r[k] = 0.5f * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                       (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3);
}
