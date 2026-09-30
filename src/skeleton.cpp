#include "forma/skeleton.hpp"

#include "forma/predicates.hpp"
#include "forma/sample.hpp"

#include <algorithm>
#include <cmath>

namespace forma {
namespace {

float falloff(float distance, float radius) {
    if (radius <= 0.f) {
        return 0.f;
    }
    const float t = std::clamp(1.f - distance / radius, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float boneDistance(const NodePool& pool, FrameId bone, Vec2 point) {
    const Vec2 origin = pool.frameBind(bone).apply({0.f, 0.f});
    float best = 1e30f;
    bool child = false;
    for (uint32_t i = 0; i < pool.frameCount(); ++i) {
        const FrameId candidate{i};
        if (pool.parent(candidate) != bone) {
            continue;
        }
        child = true;
        const Vec2 end = pool.frameBind(candidate).apply({0.f, 0.f});
        best = std::min(best, pointSegmentDistance(point, origin, end));
    }
    if (!child) {
        best = distance(point, origin);
    }
    return best;
}

}  // namespace

std::vector<Bone> bones(const NodePool& pool) {
    std::vector<Bone> out;
    out.reserve(pool.frameCount());
    for (uint32_t i = 0; i < pool.frameCount(); ++i) {
        const FrameId frame{i};
        const FrameId parent = pool.parent(frame);
        if (!parent.valid()) {
            continue;
        }
        Bone bone;
        bone.frame = frame;
        bone.from = pool.frameWorld(parent).apply({0.f, 0.f});
        bone.to = pool.frameWorld(frame).apply({0.f, 0.f});
        out.push_back(bone);
    }
    return out;
}

std::vector<Vec2> deformSkin(const NodePool& pool, std::span<const Vec2> restInRoot,
                             std::span<const BoneInfluence> influences) {
    std::vector<Vec2> out;
    out.reserve(restInRoot.size());
    const Affine rootBind = pool.frameBind(pool.root());
    std::vector<Affine> inverseBind(influences.size());
    std::vector<bool> invertible(influences.size(), false);
    for (std::size_t i = 0; i < influences.size(); ++i) {
        if (influences[i].radius <= 0.f) {
            continue;
        }
        invertible[i] = invert(pool.frameBind(influences[i].bone), inverseBind[i]);
    }
    for (Vec2 rest : restInRoot) {
        const Vec2 bindPoint = rootBind.apply(rest);
        Vec2 sum{};
        float weightSum = 0.f;
        for (std::size_t i = 0; i < influences.size(); ++i) {
            if (!invertible[i]) {
                continue;
            }
            const float weight = falloff(boneDistance(pool, influences[i].bone, bindPoint), influences[i].radius);
            if (weight <= 0.f) {
                continue;
            }
            const Vec2 local = inverseBind[i].apply(bindPoint);
            sum += pool.frameWorld(influences[i].bone).apply(local) * weight;
            weightSum += weight;
        }
        if (weightSum <= 1e-6f) {
            out.push_back(pool.frameWorld(pool.root()).apply(rest));
        } else {
            out.push_back(sum * (1.f / weightSum));
        }
    }
    return out;
}

std::vector<BoneInfluence> boneInfluences(const NodePool& pool) {
    std::vector<BoneInfluence> influences;
    for (uint32_t i = 0; i < pool.frameCount(); ++i) {
        const FrameId frame{i};
        const float radius = pool.influence(frame);
        if (radius > 0.f) {
            influences.push_back(BoneInfluence{frame, radius});
        }
    }
    return influences;
}

std::vector<Vec2> skinRest(const NodePool& pool, std::span<const NodeId> nodes) {
    std::vector<Vec2> rest;
    rest.reserve(nodes.size());
    const Affine rootBind = pool.frameBind(pool.root());
    Affine inverseRoot;
    const bool ok = invert(rootBind, inverseRoot);
    for (NodeId id : nodes) {
        const Vec2 bound = pool.frameBind(pool.frame(id)).apply(pool.rest(id));
        rest.push_back(ok ? inverseRoot.apply(bound) : bound);
    }
    return rest;
}

Polyline2 deformSkinLine(const NodePool& pool, std::span<const NodeId> nodes, bool closed) {
    Polyline2 line;
    line.closed = closed;
    const std::vector<Vec2> rest = skinRest(pool, nodes);
    line.pts = deformSkin(pool, rest, boneInfluences(pool));
    return line;
}

Polyline2 deformSkinCurve(const NodePool& pool, std::span<const NodeId> nodes, bool closed, float curve,
                          CurveParameterization parameterization, float chordError) {
    const std::vector<Vec2> rest = skinRest(pool, nodes);
    Polyline2 line = sampleCurve(rest, closed, curve, parameterization, chordError);
    line.pts = deformSkin(pool, line.pts, boneInfluences(pool));
    line.closed = closed;
    return line;
}

}  // namespace forma
