#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tiny3d {

constexpr float pi = 3.14159265358979323846f;

struct Vector3 {
    float x = 0, y = 0, z = 0;
    Vector3 operator+(Vector3 v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vector3 operator-(Vector3 v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
using Vec3 = Vector3; // Compatibility with existing games.

inline float dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vector3 cross(Vector3 a, Vector3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vector3 v) { return std::sqrt(dot(v, v)); }
inline Vector3 normalized(Vector3 v) {
    const float n = length(v);
    return n > 0 ? v * (1 / n) : Vector3{};
}

struct Quaternion {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Quaternion() = default;
    constexpr Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Quaternion operator*(Quaternion q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w,
                w * q.w - x * q.x - y * q.y - z * q.z};
    }
    static Quaternion axisAngle(Vector3 axis, float radians) {
        axis = normalized(axis);
        if (length(axis) == 0) return {};
        const float s = std::sin(radians * .5f);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * .5f)};
    }
    // Euler radians, applied X, then Y, then Z. The camera looks along +Z.
    static Quaternion fromEuler(Vector3 radians) {
        return axisAngle({0, 0, 1}, radians.z) * axisAngle({0, 1, 0}, radians.y) *
            axisAngle({1, 0, 0}, radians.x);
    }
    static Quaternion fromEulerDegrees(Vector3 degrees) { return fromEuler(degrees * (pi / 180)); }
    Vector3 toEuler() const;
    Vector3 toEulerDegrees() const { return toEuler() * (180 / pi); }
};

inline Quaternion normalized(Quaternion q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 0 ? Quaternion{q.x / n, q.y / n, q.z / n, q.w / n} : Quaternion{};
}
inline Quaternion inverse(Quaternion q) {
    const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return n > 0 ? Quaternion{-q.x / n, -q.y / n, -q.z / n, q.w / n} : Quaternion{};
}
inline Vector3 rotate(Vector3 v, Quaternion rotation) {
    const Quaternion q = normalized(rotation);
    const Vector3 axis{q.x, q.y, q.z};
    const Vector3 t = cross(axis, v) * 2;
    return v + t * q.w + cross(axis, t);
}
inline Vector3 inverseRotate(Vector3 v, Quaternion rotation) { return rotate(v, inverse(rotation)); }
inline Vector3 rotate(Vector3 v, Vector3 radians) { return rotate(v, Quaternion::fromEuler(radians)); }
inline Vector3 inverseRotate(Vector3 v, Vector3 radians) { return inverseRotate(v, Quaternion::fromEuler(radians)); }
inline Vector3 rotateX(Vector3 v, float a) { return rotate(v, Quaternion::axisAngle({1, 0, 0}, a)); }
inline Vector3 rotateY(Vector3 v, float a) { return rotate(v, Quaternion::axisAngle({0, 1, 0}, a)); }
inline Vector3 rotateZ(Vector3 v, float a) { return rotate(v, Quaternion::axisAngle({0, 0, 1}, a)); }

inline Vector3 Quaternion::toEuler() const {
    const Quaternion q = normalized(*this);
    const float sinY = std::clamp(2 * (q.w * q.y - q.z * q.x), -1.f, 1.f);
    if (std::abs(sinY) > .999999f) {
        return {0, std::copysign(pi * .5f, sinY),
            std::atan2(2 * (q.w * q.z - q.x * q.y), 1 - 2 * (q.x * q.x + q.z * q.z))};
    }
    return {std::atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)),
            std::asin(sinY),
            std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))};
}

struct Entity;

struct Transform {
    Vector3 localPosition{};
    Quaternion localRotation{};
    Vector3 localScale{1, 1, 1};

    Transform() = default;
    Transform(const Transform&) = default;
    // Assignment copies local values; the owning Entity keeps its hierarchy link.
    Transform& operator=(const Transform& value) {
        localPosition = value.localPosition;
        localRotation = value.localRotation;
        localScale = value.localScale;
        return *this;
    }

    Vector3 position() const { return parent_ ? parent_->point(localPosition) : localPosition; }
    Quaternion rotation() const {
        return normalized(parent_ ? parent_->rotation() * localRotation : localRotation);
    }
    void setPosition(Vector3 world) { localPosition = parent_ ? parent_->inversePoint(world) : world; }
    void setRotation(Quaternion world) {
        localRotation = normalized(parent_ ? inverse(parent_->rotation()) * world : world);
    }
    Vector3 lossyScale() const { return {length(vector({1, 0, 0})), length(vector({0, 1, 0})), length(vector({0, 0, 1}))}; }
    Vector3 eulerAngles() const { return rotation().toEuler(); }
    Vector3 localEulerAngles() const { return localRotation.toEuler(); }
    void setEulerAngles(Vector3 radians) { setRotation(Quaternion::fromEuler(radians)); }
    void setLocalEulerAngles(Vector3 radians) { localRotation = Quaternion::fromEuler(radians); }
    Vector3 right() const { return rotate({1, 0, 0}, rotation()); }
    Vector3 up() const { return rotate({0, 1, 0}, rotation()); }
    Vector3 forward() const { return rotate({0, 0, 1}, rotation()); }

    Vector3 vector(Vector3 v) const {
        v = rotate({v.x * localScale.x, v.y * localScale.y, v.z * localScale.z}, localRotation);
        return parent_ ? parent_->vector(v) : v;
    }
    Vector3 point(Vector3 v) const {
        v = localPosition + rotate({v.x * localScale.x, v.y * localScale.y, v.z * localScale.z}, localRotation);
        return parent_ ? parent_->point(v) : v;
    }
    Vector3 inversePoint(Vector3 v) const {
        if (localScale.x == 0 || localScale.y == 0 || localScale.z == 0) {
            throw std::invalid_argument("Cannot invert a transform with zero scale");
        }
        if (parent_) v = parent_->inversePoint(v);
        v = inverseRotate(v - localPosition, localRotation);
        return {v.x / localScale.x, v.y / localScale.y, v.z / localScale.z};
    }

private:
    friend struct Entity;
    const Transform* parent_ = nullptr;
};

} // namespace tiny3d
