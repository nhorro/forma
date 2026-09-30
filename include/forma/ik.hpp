#pragma once

#include "forma/node_pool.hpp"

#include <optional>
#include <span>

namespace forma {

struct IkOptions {
    int iterations = 8;
    float epsilon = 0.05f;
    /// World-space point on the side the limb should bend toward.
    /// For a two-bone chain this picks the knee. If it is empty, a hinge whose
    /// limits are not symmetric bends toward the allowed side; otherwise the
    /// current bend is kept.
    std::optional<Vec2> pole{};
    bool clamp = true;
};

struct IkResult {
    bool reached = false;
    float error = 0.f;
};

/// `chain` is parent-to-child, the root of the limb first.
/// The end effector is `tipLocal` in the last frame. A zero tip means the last
/// frame's origin is the effector, and that frame is not rotated.
/// Only rotations are written. One call is one limb: a human or a quadruped is
/// four calls, after the body has been placed.
IkResult solveIk(NodePool& pool, std::span<const FrameId> chain, Vec2 tipLocal, Vec2 target,
                 const IkOptions& options = {});

}  // namespace forma
