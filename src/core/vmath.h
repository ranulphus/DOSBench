/* vmath.h - the small amount of 3D maths DOSBench needs. Matrices are
 * column-major, as OpenGL takes them, so rb_gl.c passes them straight to
 * glLoadMatrixf and rb_glide.c multiplies with the same layout. */
#ifndef VMATH_H
#define VMATH_H

#define VM_PI 3.14159265358979323846

typedef struct { float m[16]; } mat4;

void m4_identity(mat4 *m);
void m4_mul(mat4 *r, const mat4 *a, const mat4 *b);            /* r = a * b (r may alias) */
void m4_perspective(mat4 *m, float fovy_deg, float aspect, float znear, float zfar);
void m4_frustum(mat4 *m, float l, float r, float b, float t, float n, float f);
void m4_lookat(mat4 *m, const float eye[3], const float at[3], const float up[3]);
void m4_translate(mat4 *m, float x, float y, float z);          /* m = m * T */
void m4_scale(mat4 *m, float x, float y, float z);              /* m = m * S */
void m4_rotate_y(mat4 *m, float deg);                           /* m = m * Ry */
void m4_rotate_x(mat4 *m, float deg);                           /* m = m * Rx */
void m4_rotate_z(mat4 *m, float deg);                           /* m = m * Rz */
void m4_rotate_axis(mat4 *m, const float axis[3], float deg);   /* m = m * R(axis) */
/* m = m * Ry(yaw) * Rx(pitch) * Rz(roll): models face +z with +y up, so yaw
 * 90 turns them to face +x, pitch 90 to face -y. */
void m4_ypr(mat4 *m, float yaw, float pitch, float roll);
/* A model's placement: +z along fwd, +y towards up, scaled by s, at pos. */
void m4_from_frame(mat4 *m, const float pos[3], const float fwd[3], const float up[3], float s);
/* out = the transposed upper 3x3 of m times v (into a rotation's frame). */
void m4_rot_inv(const mat4 *m, const float v[3], float out[3]);
/* out = m * (v, 1) */
void m4_xform(const mat4 *m, const float v[3], float out[4]);

void v3_sub(float r[3], const float a[3], const float b[3]);
void v3_cross(float r[3], const float a[3], const float b[3]);
float v3_dot(const float a[3], const float b[3]);
void v3_norm(float v[3]);
/* Catmull-Rom interpolation between p1 and p2 (t in 0..1). */
void v3_catmull(float r[3], const float p0[3], const float p1[3], const float p2[3], const float p3[3], float t);

#endif
