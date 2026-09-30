#pragma once

#include <cmath>

namespace tiny3d {

constexpr float pi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(Vec3 v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalized(Vec3 v) {
    const float n = length(v);
    return n > 0 ? v * (1 / n) : Vec3{};
}

inline Vec3 rotateX(Vec3 v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {v.x, c * v.y - s * v.z, s * v.y + c * v.z};
}
inline Vec3 rotateY(Vec3 v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}
inline Vec3 rotateZ(Vec3 v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
}
// Euler angles in radians: X, then Y, then Z. The camera looks along +Z.
inline Vec3 rotate(Vec3 v, Vec3 angles) {
    return rotateZ(rotateY(rotateX(v, angles.x), angles.y), angles.z);
}
inline Vec3 inverseRotate(Vec3 v, Vec3 angles) {
    return rotateX(rotateY(rotateZ(v, -angles.z), -angles.y), -angles.x);
}

struct Transform {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1, 1, 1};

    Vec3 point(Vec3 v) const {
        return position + rotate({v.x * scale.x, v.y * scale.y, v.z * scale.z}, rotation);
    }
};

} // namespace tiny3d
