/* 3차원 벡터·쿼터니언 (시뮬레이터 전용, double) */
#ifndef VECMATH_H
#define VECMATH_H

#include <math.h>

typedef struct { double x, y, z; } v3;
typedef struct { double w, x, y, z; } quat;

static inline v3 v3_(double x, double y, double z) { v3 r = {x, y, z}; return r; }
static inline v3 v3_add(v3 a, v3 b) { return v3_(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 v3_sub(v3 a, v3 b) { return v3_(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 v3_scale(v3 a, double s) { return v3_(a.x * s, a.y * s, a.z * s); }
static inline double v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline double v3_norm(v3 a) { return sqrt(v3_dot(a, a)); }
static inline v3 v3_cross(v3 a, v3 b)
{
    return v3_(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline v3 v3_unit(v3 a)
{
    const double n = v3_norm(a);
    return n > 0.0 ? v3_scale(a, 1.0 / n) : a;
}

static inline quat q_mul(quat a, quat b)
{
    quat r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}
static inline quat q_conj(quat a) { quat r = {a.w, -a.x, -a.y, -a.z}; return r; }
static inline quat q_normalize(quat a)
{
    const double n = sqrt(a.w * a.w + a.x * a.x + a.y * a.y + a.z * a.z);
    quat r = {a.w / n, a.x / n, a.y / n, a.z / n};
    return r;
}
/* q: 기체→관성(ECI) 회전.  v_eci = q ⊗ v_body ⊗ q* */
static inline v3 q_rot(quat q, v3 v)
{
    quat p = {0.0, v.x, v.y, v.z};
    quat r = q_mul(q_mul(q, p), q_conj(q));
    return v3_(r.x, r.y, r.z);
}
static inline v3 q_rot_inv(quat q, v3 v) { return q_rot(q_conj(q), v); }

#endif
