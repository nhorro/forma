#pragma once

#include "forma/vec2.hpp"

#include <cmath>

namespace forma {

/// A frame's local placement. `rotation` is radians.
/// Positive rotation is clockwise on a Y-down screen: the matrix is the standard
/// one, and +Y points down, so a small positive angle swings +X toward +Y.
struct FramePose {
    Vec2 translation{};
    float rotation = 0.f;
    Vec2 scale{1.f, 1.f};
};

/// Column-ish affine: p' = (m00, m10) * x + (m01, m11) * y + (tx, ty).
struct Affine {
    float m00 = 1.f;
    float m01 = 0.f;
    float m10 = 0.f;
    float m11 = 1.f;
    float tx = 0.f;
    float ty = 0.f;

    constexpr Vec2 apply(Vec2 p) const {
        return {m00 * p.x + m01 * p.y + tx, m10 * p.x + m11 * p.y + ty};
    }
};

inline Affine mul(const Affine& a, const Affine& b) {
    Affine r;
    r.m00 = a.m00 * b.m00 + a.m01 * b.m10;
    r.m01 = a.m00 * b.m01 + a.m01 * b.m11;
    r.m10 = a.m10 * b.m00 + a.m11 * b.m10;
    r.m11 = a.m10 * b.m01 + a.m11 * b.m11;
    r.tx = a.m00 * b.tx + a.m01 * b.ty + a.tx;
    r.ty = a.m10 * b.tx + a.m11 * b.ty + a.ty;
    return r;
}

/// Rotation then scale, no translation. Scale is along the local axes, before rotation.
inline Affine linearRS(float rotation, Vec2 scale) {
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    Affine a;
    a.m00 = c * scale.x;
    a.m01 = -s * scale.y;
    a.m10 = s * scale.x;
    a.m11 = c * scale.y;
    return a;
}

inline Affine trs(const FramePose& pose) {
    Affine a = linearRS(pose.rotation, pose.scale);
    a.tx = pose.translation.x;
    a.ty = pose.translation.y;
    return a;
}

inline bool invert(const Affine& a, Affine& out) {
    const float det = a.m00 * a.m11 - a.m01 * a.m10;
    if (std::fabs(det) < 1e-12f) {
        return false;
    }
    const float inv = 1.f / det;
    out.m00 = a.m11 * inv;
    out.m01 = -a.m01 * inv;
    out.m10 = -a.m10 * inv;
    out.m11 = a.m00 * inv;
    out.tx = -(out.m00 * a.tx + out.m01 * a.ty);
    out.ty = -(out.m10 * a.tx + out.m11 * a.ty);
    return true;
}

}  // namespace forma
