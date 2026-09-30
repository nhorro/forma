#pragma once

#include "forma/geometry.hpp"

#include <box2d/box2d.h>

#include <span>
#include <vector>

namespace forma {

/// Pixel-space model to Box2D meters. The node pool stays in pixels.
struct PhysicsScale {
    float pixelsPerMeter = 32.f;
    bool flipY = true;
};

b2Vec2 toWorld(Vec2 screen, PhysicsScale scale);
Vec2 toScreen(b2Vec2 world, PhysicsScale scale);

/// World-space direction of a chain's front face (the side that collides).
/// With `flipY` and Box2D's default gravity of (0, -10), a ground line wants `PositiveY`
/// so bodies falling toward +screen-Y land on it.
enum class ChainFront { PositiveY, NegativeY };

struct ChainPoints {
    std::vector<b2Vec2> points;
};

/// Open chain in body-local meters. Pads the ends, because Box2D open chains
/// do not collide on the first and last segments, and needs at least 4 points.
/// `screenLine` is not closed. Points are cloned by Box2D at creation time.
ChainPoints buildChainPoints(const Polyline2& screenLine, PhysicsScale scale, ChainFront front,
                             float padPixels = 80.f);

b2ChainId createChain(b2BodyId body, const ChainPoints& points);

struct ConvexBody {
    bool ok = false;
    bool simplified = false;  ///< Hull dropped or reordered vertices to satisfy Box2D's 8-vertex convex limit.
    b2Vec2 position{};         ///< World-space centroid. Create the body here.
    std::vector<b2Vec2> local; ///< Input vertices relative to `position`, original order, for write-back.
    b2Polygon polygon{};       ///< Convex hull, local to `position`.
};

/// Builds a convex fixture. Concave input is replaced by its hull.
/// More than 8 hull vertices are uniformly reduced. `b2ComputeHull` rejects larger sets.
ConvexBody buildConvexBody(std::span<const Vec2> screenPoints, PhysicsScale scale);

/// Circle in body-local meters, centered on the body origin.
b2Circle buildCircle(float radiusPixels, PhysicsScale scale);

/// One fixture per convex part. Concave outlines keep their notches, up to 8 vertices each.
/// Each part is its own body, centered on that part. For a static level, use `attachConvexParts`.
std::vector<ConvexBody> buildConvexParts(std::span<const Vec2> screenPoints, PhysicsScale scale);

/// Attach every convex part of a screen-space outline to one body, in that body's local frame.
/// A concave level keeps its notches. Returns how many polygon shapes were created.
int attachConvexParts(b2BodyId body, std::span<const Vec2> screenRing, PhysicsScale scale);

struct BoneBody {
    FrameId frame{};
    b2BodyId id{};
};

struct HingeJoint {
    FrameId child{};
    b2JointId id{};
};

struct Ragdoll {
    std::vector<BoneBody> bodies;
    std::vector<HingeJoint> joints;
};

struct RagdollOptions {
    float halfWidth = 6.f;
    float density = 1.f;
    bool limits = true;
    /// The root body is kinematic, so the limbs hang off a placed torso.
    bool pinRoot = false;
};

/// One dynamic body per frame and a revolute joint at each child pivot.
/// Hinge limits use the frame limits. Forma angles are clockwise and Y-down;
/// the joint angles are negated to match Box2D.
Ragdoll createRagdoll(b2WorldId world, const NodePool& pool, PhysicsScale scale, const RagdollOptions& options = {});

}  // namespace forma
