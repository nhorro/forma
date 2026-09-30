#pragma once

#include <cmath>
#include <cstdint>

namespace forma {

struct Vec2 {
    float x = 0.f;
    float y = 0.f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }

    constexpr Vec2& operator+=(Vec2 o) {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2& operator-=(Vec2 o) {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    constexpr Vec2& operator*=(float s) {
        x *= s;
        y *= s;
        return *this;
    }
};

constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

/// Standard cross product. Positive when `b` is left of `a` in Y-up coordinates.
constexpr float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

constexpr float cross(Vec2 a, Vec2 b, Vec2 c) { return cross(b - a, c - a); }

inline float lengthSq(Vec2 v) { return dot(v, v); }

inline float length(Vec2 v) { return std::sqrt(lengthSq(v)); }

inline Vec2 normalized(Vec2 v) {
    const float len = length(v);
    return len > 1e-8f ? v / len : Vec2{0.f, 0.f};
}

/// Left perpendicular in Y-up coordinates: (-y, x).
constexpr Vec2 perp(Vec2 v) { return {-v.y, v.x}; }

constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

inline float distance(Vec2 a, Vec2 b) { return length(b - a); }

inline float pointSegmentDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float den = lengthSq(ab);
    if (den < 1e-12f) {
        return distance(p, a);
    }
    float t = dot(p - a, ab) / den;
    if (t < 0.f) {
        t = 0.f;
    } else if (t > 1.f) {
        t = 1.f;
    }
    return distance(p, a + ab * t);
}

}  // namespace forma
