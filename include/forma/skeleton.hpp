#pragma once

#include "forma/geometry.hpp"

#include <span>
#include <vector>

namespace forma {

struct Bone {
    FrameId frame{};
    Vec2 from{};
    Vec2 to{};
};

struct BoneInfluence {
    FrameId bone{};
    float radius = 0.f;
};

/// One segment per non-root frame, from the parent origin to this frame's origin.
std::vector<Bone> bones(const NodePool& pool);

/// Linear-blend skin. `restInRoot` is in the root frame's bind space.
/// A point with no weight is left on the bind pose. Output is world space.
/// Weights come from distance to each bone segment, with a smooth falloff inside `radius`.
std::vector<Vec2> deformSkin(const NodePool& pool, std::span<const Vec2> restInRoot,
                             std::span<const BoneInfluence> influences);

}  // namespace forma
