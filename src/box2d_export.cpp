#include "forma/box2d_export.hpp"

#include "forma/decompose.hpp"
#include "forma/predicates.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace forma {
namespace {

constexpr int kMaxVertices = B2_MAX_POLYGON_VERTICES;

b2Vec2 frontNormal(std::span<const b2Vec2> points) {
    b2Vec2 front{0.f, 0.f};
    if (points.size() < 2) {
        return front;
    }
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const b2Vec2 d{points[i + 1].x - points[i].x, points[i + 1].y - points[i].y};
        // Right of travel in Y-up is the chain front face.
        front.x += d.y;
        front.y += -d.x;
    }
    return front;
}

std::vector<b2Vec2> dedup(std::vector<b2Vec2> points) {
    constexpr float kMin = 0.02f;  // meters, comfortably above B2_LINEAR_SLOP
    std::vector<b2Vec2> out;
    out.reserve(points.size());
    for (b2Vec2 p : points) {
        if (!out.empty()) {
            const float dx = p.x - out.back().x;
            const float dy = p.y - out.back().y;
            if (dx * dx + dy * dy < kMin * kMin) {
                continue;
            }
        }
        out.push_back(p);
    }
    return out;
}

std::vector<Vec2> reduceHull(std::vector<Vec2> hull) {
    if (static_cast<int>(hull.size()) <= kMaxVertices) {
        return hull;
    }
    std::vector<Vec2> reduced;
    reduced.reserve(static_cast<std::size_t>(kMaxVertices));
    const int n = static_cast<int>(hull.size());
    for (int i = 0; i < kMaxVertices; ++i) {
        reduced.push_back(hull[static_cast<std::size_t>((i * n) / kMaxVertices)]);
    }
    return reduced;
}

}  // namespace

b2Vec2 toWorld(Vec2 screen, PhysicsScale scale) {
    const float inv = scale.pixelsPerMeter == 0.f ? 0.f : 1.f / scale.pixelsPerMeter;
    const float y = scale.flipY ? -screen.y : screen.y;
    return b2Vec2{screen.x * inv, y * inv};
}

Vec2 toScreen(b2Vec2 world, PhysicsScale scale) {
    float y = world.y * scale.pixelsPerMeter;
    if (scale.flipY) {
        y = -y;
    }
    return {world.x * scale.pixelsPerMeter, y};
}

ChainPoints buildChainPoints(const Polyline2& screenLine, PhysicsScale scale, ChainFront front, float padPixels) {
    ChainPoints built;
    std::vector<Vec2> pts = screenLine.pts;
    if (pts.size() >= 2 && distance(pts.front(), pts.back()) < 1e-3f) {
        pts.pop_back();
    }
    if (pts.size() >= 2 && padPixels > 0.f) {
        Vec2 startDir = normalized(pts[1] - pts[0]);
        if (lengthSq(startDir) < 1e-8f) {
            startDir = {1.f, 0.f};
        }
        Vec2 endDir = normalized(pts.back() - pts[pts.size() - 2]);
        if (lengthSq(endDir) < 1e-8f) {
            endDir = {1.f, 0.f};
        }
        pts.insert(pts.begin(), pts.front() - startDir * padPixels);
        pts.push_back(pts.back() + endDir * padPixels);
    }

    std::vector<b2Vec2> world;
    world.reserve(pts.size());
    for (Vec2 p : pts) {
        world.push_back(toWorld(p, scale));
    }
    world = dedup(std::move(world));
    const b2Vec2 facing = frontNormal(world);
    const bool wantPositive = front == ChainFront::PositiveY;
    if ((wantPositive && facing.y < 0.f) || (!wantPositive && facing.y > 0.f)) {
        std::reverse(world.begin(), world.end());
    }
    built.points = std::move(world);
    return built;
}

b2ChainId createChain(b2BodyId body, const ChainPoints& points) {
    if (static_cast<int>(points.points.size()) < 4) {
        return b2_nullChainId;
    }
    b2ChainDef def = b2DefaultChainDef();
    def.points = points.points.data();
    def.count = static_cast<int>(points.points.size());
    def.isLoop = false;
    return b2CreateChain(body, &def);
}

ConvexBody buildConvexBody(std::span<const Vec2> screenPoints, PhysicsScale scale) {
    ConvexBody body;
    if (screenPoints.size() < 3) {
        return body;
    }
    const Vec2 centroid = polygonCentroid(screenPoints);
    body.position = toWorld(centroid, scale);
    body.local.reserve(screenPoints.size());
    std::vector<Vec2> world;
    world.reserve(screenPoints.size());
    for (Vec2 p : screenPoints) {
        const b2Vec2 w = toWorld(p, scale);
        body.local.push_back(b2Vec2{w.x - body.position.x, w.y - body.position.y});
        world.push_back({w.x, w.y});
    }

    std::vector<Vec2> hull = convexHull(world);
    if (hull.size() < 3) {
        return body;
    }
    if (hull.size() != screenPoints.size()) {
        body.simplified = true;
    }
    if (static_cast<int>(hull.size()) > kMaxVertices) {
        hull = reduceHull(std::move(hull));
        body.simplified = true;
    }

    std::vector<b2Vec2> localHull;
    localHull.reserve(hull.size());
    for (Vec2 p : hull) {
        localHull.push_back(b2Vec2{p.x - body.position.x, p.y - body.position.y});
    }
    const b2Hull computed = b2ComputeHull(localHull.data(), static_cast<int>(localHull.size()));
    if (computed.count < 3) {
        return body;
    }
    body.polygon = b2MakePolygon(&computed, 0.f);
    body.ok = true;
    return body;
}

b2Circle buildCircle(float radiusPixels, PhysicsScale scale) {
    b2Circle circle{};
    circle.center = {0.f, 0.f};
    circle.radius = std::max(radiusPixels, 0.f) / std::max(scale.pixelsPerMeter, 1e-6f);
    return circle;
}

std::vector<ConvexBody> buildConvexParts(std::span<const Vec2> screenPoints, PhysicsScale scale) {
    std::vector<ConvexBody> bodies;
    for (const std::vector<Vec2>& part : convexParts(screenPoints)) {
        ConvexBody body = buildConvexBody(part, scale);
        if (body.ok) {
            bodies.push_back(std::move(body));
        }
    }
    return bodies;
}

int attachConvexParts(b2BodyId body, std::span<const Vec2> screenRing, PhysicsScale scale) {
    if (!b2Body_IsValid(body)) {
        return 0;
    }
    const b2Vec2 origin = b2Body_GetPosition(body);
    const b2Rot rotation = b2Body_GetRotation(body);
    b2ShapeDef shape = b2DefaultShapeDef();
    shape.density = 0.f;
    shape.material.friction = 0.8f;
    int created = 0;
    for (const std::vector<Vec2>& part : convexParts(screenRing)) {
        std::vector<b2Vec2> local;
        local.reserve(part.size());
        for (Vec2 point : part) {
            const b2Vec2 world = toWorld(point, scale);
            const b2Vec2 delta{world.x - origin.x, world.y - origin.y};
            local.push_back(b2InvRotateVector(rotation, delta));
        }
        if (static_cast<int>(local.size()) < 3) {
            continue;
        }
        const b2Hull hull = b2ComputeHull(local.data(), static_cast<int>(local.size()));
        if (hull.count < 3) {
            continue;
        }
        const b2Polygon polygon = b2MakePolygon(&hull, 0.f);
        b2CreatePolygonShape(body, &shape, &polygon);
        ++created;
    }
    return created;
}

namespace {

constexpr float kPi = 3.14159265358979323846f;

float worldAngle(const NodePool& pool, FrameId frame) {
    const Affine world = pool.frameWorld(frame);
    const Vec2 axis = world.apply({1.f, 0.f}) - world.apply({0.f, 0.f});
    return std::atan2(axis.y, axis.x);
}

float localLength(const NodePool& pool, FrameId frame) {
    const Vec2 origin = pool.frameWorld(frame).apply({0.f, 0.f});
    float best = 0.f;
    for (uint32_t i = 0; i < pool.frameCount(); ++i) {
        const FrameId candidate{i};
        if (pool.parent(candidate) != frame) {
            continue;
        }
        const Vec2 local = pool.toLocal(frame, pool.frameWorld(candidate).apply({0.f, 0.f}));
        best = std::max(best, length(local));
        (void)origin;
    }
    return best;
}

b2Vec2 meters(Vec2 formaLocal, float pixelsPerMeter) {
    return {formaLocal.x / pixelsPerMeter, -formaLocal.y / pixelsPerMeter};
}

}  // namespace

Ragdoll createRagdoll(b2WorldId world, const NodePool& pool, PhysicsScale scale, const RagdollOptions& options) {
    Ragdoll ragdoll;
    const float ppm = std::max(scale.pixelsPerMeter, 1e-6f);
    const float width = std::max(options.halfWidth, 1.f);
    for (uint32_t i = 0; i < pool.frameCount(); ++i) {
        const FrameId frame{i};
        const float length = std::max(localLength(pool, frame), width * 2.f);
        const Vec2 corners[] = {{0.f, -width}, {length, -width}, {length, width}, {0.f, width}};
        b2Vec2 local[4];
        for (int c = 0; c < 4; ++c) {
            local[c] = meters(corners[c], ppm);
        }
        const b2Hull hull = b2ComputeHull(local, 4);
        b2BodyDef def = b2DefaultBodyDef();
        def.type = options.pinRoot && frame == pool.root() ? b2_kinematicBody : b2_dynamicBody;
        const Vec2 origin = pool.frameWorld(frame).apply({0.f, 0.f});
        def.position = toWorld(origin, scale);
        def.rotation = b2MakeRot(-worldAngle(pool, frame));
        const b2BodyId body = b2CreateBody(world, &def);
        if (hull.count >= 3) {
            const b2Polygon polygon = b2MakePolygon(&hull, 0.f);
            b2ShapeDef shape = b2DefaultShapeDef();
            shape.density = options.density;
            shape.material.friction = 0.6f;
            b2CreatePolygonShape(body, &shape, &polygon);
        }
        ragdoll.bodies.push_back(BoneBody{frame, body});
    }
    auto bodyOf = [&](FrameId frame) {
        for (const BoneBody& bone : ragdoll.bodies) {
            if (bone.frame == frame) {
                return bone.id;
            }
        }
        return b2_nullBodyId;
    };
    for (uint32_t i = 0; i < pool.frameCount(); ++i) {
        const FrameId child{i};
        const FrameId parent = pool.parent(child);
        if (!parent.valid()) {
            continue;
        }
        b2RevoluteJointDef joint = b2DefaultRevoluteJointDef();
        joint.bodyIdA = bodyOf(parent);
        joint.bodyIdB = bodyOf(child);
        const Vec2 anchor = pool.frameWorld(child).apply({0.f, 0.f});
        joint.localAnchorA = meters(pool.toLocal(parent, anchor), ppm);
        joint.localAnchorB = {0.f, 0.f};
        joint.referenceAngle = -worldAngle(pool, child) - (-worldAngle(pool, parent));
        joint.collideConnected = false;
        joint.drawSize = 0.15f;
        if (options.limits && pool.hasLimits(child)) {
            const float cap = 0.99f * kPi;
            const float lower = std::clamp(-pool.limitMax(child), -cap, cap);
            const float upper = std::clamp(-pool.limitMin(child), -cap, cap);
            joint.enableLimit = true;
            joint.lowerAngle = std::min(lower, upper);
            joint.upperAngle = std::max(lower, upper);
        }
        ragdoll.joints.push_back(HingeJoint{child, b2CreateRevoluteJoint(world, &joint)});
    }
    return ragdoll;
}

}  // namespace forma
