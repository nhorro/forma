#include "forma/ik.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace forma {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float wrapPi(float angle) {
    while (angle > kPi) {
        angle -= 2.f * kPi;
    }
    while (angle < -kPi) {
        angle += 2.f * kPi;
    }
    return angle;
}

Vec2 rotateClockwise(Vec2 v, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

Vec2 frameOrigin(const NodePool& pool, FrameId frame) { return pool.frameWorld(frame).apply({0.f, 0.f}); }

struct Segment {
    FrameId frame{};
    Vec2 localEnd{};
};

void aimSegment(NodePool& pool, const Segment& segment, Vec2 worldTarget, bool clamp) {
    const Vec2 origin = frameOrigin(pool, segment.frame);
    const Vec2 worldDir = worldTarget - origin;
    if (length(worldDir) < 1e-5f) {
        return;
    }
    const FrameId parent = pool.parent(segment.frame);
    Affine linear = parent.valid() ? pool.frameWorld(parent) : Affine{};
    linear.tx = 0.f;
    linear.ty = 0.f;
    Affine inverse;
    if (!invert(linear, inverse)) {
        return;
    }
    const Vec2 desired = inverse.apply(worldDir);
    const Vec2 scale = pool.pose(segment.frame).scale;
    const Vec2 bone{segment.localEnd.x * scale.x, segment.localEnd.y * scale.y};
    if (length(bone) < 1e-5f) {
        return;
    }
    const float wanted = std::atan2(desired.y, desired.x) - std::atan2(bone.y, bone.x);
    FramePose pose = pool.pose(segment.frame);
    pose.rotation += wrapPi(wanted - pose.rotation);
    pool.setPose(segment.frame, pose);
    if (clamp) {
        pool.clampToLimits(segment.frame);
    }
}

Vec2 segmentEnd(const NodePool& pool, const Segment& segment) {
    return pool.frameWorld(segment.frame).apply(segment.localEnd);
}

std::vector<Segment> segmentsOf(const NodePool& pool, std::span<const FrameId> chain, Vec2 tipLocal, bool& tipIsOrigin) {
    if (chain.empty()) {
        throw std::invalid_argument("IK chain is empty");
    }
    for (std::size_t i = 1; i < chain.size(); ++i) {
        if (pool.parent(chain[i]) != chain[i - 1]) {
            throw std::invalid_argument("IK chain must be a parent-to-child sequence");
        }
    }
    tipIsOrigin = length(tipLocal) < 1e-3f;
    const std::size_t bones = tipIsOrigin ? chain.size() - 1 : chain.size();
    if (tipIsOrigin && chain.size() < 2) {
        return {};
    }
    std::vector<Segment> segments;
    segments.reserve(bones);
    for (std::size_t i = 0; i + 1 < chain.size() && segments.size() < bones; ++i) {
        Segment segment;
        segment.frame = chain[i];
        segment.localEnd = pool.toLocal(chain[i], frameOrigin(pool, chain[i + 1]));
        segments.push_back(segment);
    }
    if (!tipIsOrigin) {
        Segment tip;
        tip.frame = chain.back();
        tip.localEnd = tipLocal;
        segments.push_back(tip);
    }
    return segments;
}

float bendSide(const NodePool& pool, FrameId hinge, Vec2 aim, Vec2 currentMid, Vec2 root, const IkOptions& options) {
    if (options.pole) {
        return cross(aim, *options.pole - root) >= 0.f ? 1.f : -1.f;
    }
    if (pool.hasLimits(hinge)) {
        const float preference = 0.5f * (pool.limitMin(hinge) + pool.limitMax(hinge));
        // A positive child rotation swings the tip clockwise, which puts the
        // knee on the other side of the root-to-target line.
        if (std::fabs(preference) > 1e-3f) {
            return preference > 0.f ? -1.f : 1.f;
        }
    }
    const float current = cross(aim, currentMid - root);
    if (std::fabs(current) < 1e-3f) {
        return 1.f;
    }
    return current > 0.f ? 1.f : -1.f;
}

IkResult solveTwoBone(NodePool& pool, const Segment& upper, const Segment& lower, Vec2 target, const IkOptions& options) {
    const Vec2 root = frameOrigin(pool, upper.frame);
    const Vec2 mid = segmentEnd(pool, upper);
    const float upperLength = distance(root, mid);
    const float lowerLength = distance(mid, segmentEnd(pool, lower));
    if (upperLength < 1e-4f || lowerLength < 1e-4f) {
        throw std::invalid_argument("IK bone has no length");
    }
    Vec2 toTarget = target - root;
    float distanceToTarget = length(toTarget);
    if (distanceToTarget < 1e-4f) {
        toTarget = {1.f, 0.f};
        distanceToTarget = 1.f;
    }
    const float minReach = std::fabs(upperLength - lowerLength);
    const float maxReach = upperLength + lowerLength;
    const float reach = std::clamp(distanceToTarget, minReach + 1e-4f, maxReach);
    const Vec2 aim = toTarget * (reach / distanceToTarget);
    const float cosBend = std::clamp(
        (upperLength * upperLength + reach * reach - lowerLength * lowerLength) / (2.f * upperLength * reach), -1.f, 1.f);
    const float bend = std::acos(cosBend);
    const float side = bendSide(pool, lower.frame, aim, mid, root, options);
    const Vec2 wantedMid = root + rotateClockwise(aim * (1.f / reach), side * bend) * upperLength;
    aimSegment(pool, upper, wantedMid, options.clamp);
    aimSegment(pool, lower, target, options.clamp);
    IkResult result;
    result.error = distance(segmentEnd(pool, lower), target);
    result.reached = result.error <= options.epsilon;
    return result;
}

IkResult solveChain(NodePool& pool, const std::vector<Segment>& segments, Vec2 target, const IkOptions& options) {
    const int count = static_cast<int>(segments.size());
    std::vector<Vec2> points(static_cast<std::size_t>(count) + 1);
    std::vector<float> lengths(static_cast<std::size_t>(count));
    points[0] = frameOrigin(pool, segments[0].frame);
    for (int i = 0; i < count; ++i) {
        points[static_cast<std::size_t>(i) + 1] = segmentEnd(pool, segments[static_cast<std::size_t>(i)]);
        lengths[static_cast<std::size_t>(i)] = distance(points[static_cast<std::size_t>(i)], points[static_cast<std::size_t>(i) + 1]);
        if (lengths[static_cast<std::size_t>(i)] < 1e-4f) {
            throw std::invalid_argument("IK bone has no length");
        }
    }
    const Vec2 root = points[0];
    const int iterations = std::max(options.iterations, 1);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        points.back() = target;
        for (int i = count - 1; i >= 0; --i) {
            const Vec2 delta = points[static_cast<std::size_t>(i)] - points[static_cast<std::size_t>(i) + 1];
            const float span = length(delta);
            const Vec2 direction = span < 1e-6f ? Vec2{1.f, 0.f} : delta * (1.f / span);
            points[static_cast<std::size_t>(i)] = points[static_cast<std::size_t>(i) + 1] + direction * lengths[static_cast<std::size_t>(i)];
        }
        points[0] = root;
        for (int i = 0; i < count; ++i) {
            Vec2 goal = points[static_cast<std::size_t>(i) + 1];
            if (options.pole && i + 1 < count) {
                goal += (*options.pole - goal) * 0.25f;
            }
            aimSegment(pool, segments[static_cast<std::size_t>(i)], goal, options.clamp);
            points[static_cast<std::size_t>(i) + 1] = segmentEnd(pool, segments[static_cast<std::size_t>(i)]);
        }
        if (distance(points.back(), target) <= options.epsilon) {
            break;
        }
    }
    IkResult result;
    result.error = distance(segmentEnd(pool, segments.back()), target);
    result.reached = result.error <= options.epsilon;
    return result;
}

}  // namespace

IkResult solveIk(NodePool& pool, std::span<const FrameId> chain, Vec2 tipLocal, Vec2 target, const IkOptions& options) {
    bool tipIsOrigin = false;
    const std::vector<Segment> segments = segmentsOf(pool, chain, tipLocal, tipIsOrigin);
    if (segments.empty()) {
        IkResult result;
        result.error = distance(frameOrigin(pool, chain.back()), target);
        result.reached = result.error <= options.epsilon;
        return result;
    }
    if (segments.size() == 2) {
        return solveTwoBone(pool, segments[0], segments[1], target, options);
    }
    return solveChain(pool, segments, target, options);
}

}  // namespace forma
