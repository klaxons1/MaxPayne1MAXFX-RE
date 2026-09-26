// Math primitives used by the MAX-FX LDB loader and the level viewer.
// Matrices coming from LDB files are stored as 4x3 row-major transforms
// (three basis vectors + translation), matching Remedy's R_Matrix4x3.
#ifndef MAXFX_CORE_MATH_H
#define MAXFX_CORE_MATH_H

#include <cmath>
#include <cstddef>

namespace maxfx {

struct Vec2 {
    float x;
    float y;

    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
    float x;
    float y;
    float z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& rhs) const { return Vec3(x + rhs.x, y + rhs.y, z + rhs.z); }
    Vec3 operator-(const Vec3& rhs) const { return Vec3(x - rhs.x, y - rhs.y, z - rhs.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    Vec3& operator+=(const Vec3& rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
};

struct Vec4 {
    float x;
    float y;
    float z;
    float w;

    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// 3x3 stored row-major: row i lives at m[i*3 + 0..2].
struct Mat3 {
    float m[9];

    Mat3() {
        for (int i = 0; i < 9; ++i) {
            m[i] = 0.0f;
        }
        m[0] = m[4] = m[8] = 1.0f;
    }
};

// Remedy 4x3 transform: rows[0..2] = rotation/scale basis, rows[3] = translation.
struct Mat4x3 {
    Vec3 rows[4];

    Mat4x3() {
        rows[0] = Vec3(1.0f, 0.0f, 0.0f);
        rows[1] = Vec3(0.0f, 1.0f, 0.0f);
        rows[2] = Vec3(0.0f, 0.0f, 1.0f);
        rows[3] = Vec3(0.0f, 0.0f, 0.0f);
    }

    Vec3 xAxis() const { return rows[0]; }
    Vec3 yAxis() const { return rows[1]; }
    Vec3 zAxis() const { return rows[2]; }
    Vec3 translation() const { return rows[3]; }
};

// Column-major 4x4, matching OpenGL / D3D upload conventions.
struct Mat4 {
    float m[16];

    Mat4() {
        for (int i = 0; i < 16; ++i) {
            m[i] = 0.0f;
        }
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    if (len <= 1.0e-8f) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    return v * (1.0f / len);
}

// p' = p * R + T  (row-vector convention used by MAX-FX)
inline Vec3 transformPoint(const Mat4x3& t, const Vec3& p) {
    return Vec3(
        p.x * t.rows[0].x + p.y * t.rows[1].x + p.z * t.rows[2].x + t.rows[3].x,
        p.x * t.rows[0].y + p.y * t.rows[1].y + p.z * t.rows[2].y + t.rows[3].y,
        p.x * t.rows[0].z + p.y * t.rows[1].z + p.z * t.rows[2].z + t.rows[3].z);
}

inline Vec3 transformVector(const Mat4x3& t, const Vec3& v) {
    return Vec3(
        v.x * t.rows[0].x + v.y * t.rows[1].x + v.z * t.rows[2].x,
        v.x * t.rows[0].y + v.y * t.rows[1].y + v.z * t.rows[2].y,
        v.x * t.rows[0].z + v.y * t.rows[1].z + v.z * t.rows[2].z);
}

// Combined = parent * child, with child translation rotated by parent.
inline Mat4x3 combine(const Mat4x3& parent, const Mat4x3& child) {
    Mat4x3 out;
    for (int i = 0; i < 3; ++i) {
        out.rows[i] = Vec3(
            parent.rows[i].x * child.rows[0].x + parent.rows[i].y * child.rows[1].x +
                parent.rows[i].z * child.rows[2].x,
            parent.rows[i].x * child.rows[0].y + parent.rows[i].y * child.rows[1].y +
                parent.rows[i].z * child.rows[2].y,
            parent.rows[i].x * child.rows[0].z + parent.rows[i].y * child.rows[1].z +
                parent.rows[i].z * child.rows[2].z);
    }
    out.rows[3] = transformPoint(parent, child.rows[3]);
    return out;
}

inline Mat4 identity4() {
    return Mat4();
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            r.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return r;
}

inline Mat4 perspectiveRH(float fovyRadians, float aspect, float zNear, float zFar) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) {
        r.m[i] = 0.0f;
    }
    const float tanHalf = std::tan(fovyRadians * 0.5f);
    r.m[0] = 1.0f / (aspect * tanHalf);
    r.m[5] = 1.0f / tanHalf;
    r.m[10] = -(zFar + zNear) / (zFar - zNear);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
    return r;
}

inline Mat4 lookAtRH(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 r;
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[12] = -dot(s, eye);

    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[13] = -dot(u, eye);

    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[14] = dot(f, eye);

    r.m[3] = 0.0f;
    r.m[7] = 0.0f;
    r.m[11] = 0.0f;
    r.m[15] = 1.0f;
    return r;
}

inline Mat4 mat4FromTransform(const Mat4x3& t) {
    Mat4 r;
    // Column-major: columns are the basis vectors.
    r.m[0] = t.rows[0].x;
    r.m[1] = t.rows[0].y;
    r.m[2] = t.rows[0].z;
    r.m[3] = 0.0f;

    r.m[4] = t.rows[1].x;
    r.m[5] = t.rows[1].y;
    r.m[6] = t.rows[1].z;
    r.m[7] = 0.0f;

    r.m[8] = t.rows[2].x;
    r.m[9] = t.rows[2].y;
    r.m[10] = t.rows[2].z;
    r.m[11] = 0.0f;

    r.m[12] = t.rows[3].x;
    r.m[13] = t.rows[3].y;
    r.m[14] = t.rows[3].z;
    r.m[15] = 1.0f;
    return r;
}

inline float toRadians(float degrees) {
    return degrees * 0.017453292519943295f;
}

inline float clamp(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

inline float wrapAngle(float a) {
    const float pi = 3.14159265358979323846f;
    const float two = pi * 2.0f;
    while (a > pi) {
        a -= two;
    }
    while (a < -pi) {
        a += two;
    }
    return a;
}

// Inverse of a rigid (rotation + translation) 4x3. Scale is treated as
// orthonormal; callers that need scaled bones should orthonormalize first.
inline Mat4x3 inverseRigid(const Mat4x3& t) {
    Mat4x3 inv;
    inv.rows[0] = Vec3(t.rows[0].x, t.rows[1].x, t.rows[2].x);
    inv.rows[1] = Vec3(t.rows[0].y, t.rows[1].y, t.rows[2].y);
    inv.rows[2] = Vec3(t.rows[0].z, t.rows[1].z, t.rows[2].z);
    const Vec3 tr = t.rows[3];
    inv.rows[3] = Vec3(-(tr.x * inv.rows[0].x + tr.y * inv.rows[1].x + tr.z * inv.rows[2].x),
                       -(tr.x * inv.rows[0].y + tr.y * inv.rows[1].y + tr.z * inv.rows[2].y),
                       -(tr.x * inv.rows[0].z + tr.y * inv.rows[1].z + tr.z * inv.rows[2].z));
    return inv;
}

inline Mat4x3 lerpMat(const Mat4x3& a, const Mat4x3& b, float t) {
    Mat4x3 out;
    for (int i = 0; i < 4; ++i) {
        out.rows[i] = lerp(a.rows[i], b.rows[i], t);
    }
    out.rows[0] = normalize(out.rows[0]);
    Vec3 y = out.rows[1];
    y = y - out.rows[0] * dot(out.rows[0], y);
    if (length(y) > 1.0e-6f) {
        out.rows[1] = normalize(y);
    }
    out.rows[2] = normalize(cross(out.rows[0], out.rows[1]));
    out.rows[1] = normalize(cross(out.rows[2], out.rows[0]));
    return out;
}

inline Mat4x3 rotationY(float yaw) {
    Mat4x3 m;
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    m.rows[0] = Vec3(c, 0.0f, -s);
    m.rows[1] = Vec3(0.0f, 1.0f, 0.0f);
    m.rows[2] = Vec3(s, 0.0f, c);
    return m;
}

inline Mat4x3 makeEntity(const Vec3& position, float yaw) {
    Mat4x3 m = rotationY(yaw);
    m.rows[3] = position;
    return m;
}

}  // namespace maxfx

#endif  // MAXFX_CORE_MATH_H
