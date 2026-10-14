// Minimal linear algebra for the demo (3-vector + 4x4 matrix, right-handed).
// The real renderer backend (WebGPU) will do this on the GPU side; this is the
// CPU fallback used by the software rasterizer for development/testing.
#pragma once
#include <cmath>
#include <cstring>

namespace math {

struct Vec3 { float x = 0, y = 0, z = 0; };

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

struct Mat4 {
    float m[16];
    Mat4() { identity(); }
    void identity() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    static Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
        Mat4 r;
        float t = std::tan(fovy * 0.5f * 3.14159265f / 180.0f);
        r.m[0] = 1.0f / (aspect * t);
        r.m[5] = 1.0f / t;
        r.m[10] = -(zfar + znear) / (zfar - znear);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * zfar * znear) / (zfar - znear);
        r.m[15] = 0.0f;
        return r;
    }
    static Mat4 rotateY(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.m[0] = c;  r.m[2] = s;
        r.m[8] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 rotateX(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.m[5] = c;  r.m[6] = -s;
        r.m[9] = s;  r.m[10] = c;
        return r;
    }
    static Mat4 translate(float x, float y, float z) {
        Mat4 r;
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[i + k * 4] * b.m[k + j * 4];
            r.m[i + j * 4] = s;
        }
    }
    return r;
}

// Transform a point; returns clip-space (w kept for perspective divide by caller).
inline Vec3 transformPoint(const Mat4& m, Vec3 v, float& w) {
    Vec3 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12];
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13];
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14];
    w   = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15];
    return r;
}

} // namespace math
