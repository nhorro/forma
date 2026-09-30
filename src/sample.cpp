#include "forma/sample.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace forma {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float alphaOf(CurveParameterization param) {
    switch (param) {
        case CurveParameterization::Uniform:
            return 0.f;
        case CurveParameterization::Chordal:
            return 1.f;
        case CurveParameterization::Centripetal:
        default:
            return 0.5f;
    }
}

float knotStep(Vec2 a, Vec2 b, float alpha) {
    if (alpha == 0.f) {
        return 1.f;
    }
    const float d = distance(a, b);
    const float v = std::pow(std::max(d, 0.f), alpha);
    return v < 1e-4f ? 1e-4f : v;
}

Vec2 lerpKnot(Vec2 a, Vec2 b, float ta, float tb, float t) {
    const float den = tb - ta;
    if (std::fabs(den) < 1e-6f) {
        return a;
    }
    const float u = (t - ta) / den;
    return a * (1.f - u) + b * u;
}

Vec2 catmull(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t, float alpha, float curve) {
    const float t0 = 0.f;
    const float t1 = knotStep(p0, p1, alpha);
    const float t2 = t1 + knotStep(p1, p2, alpha);
    const float t3 = t2 + knotStep(p2, p3, alpha);
    const float tt = t1 + (t2 - t1) * std::clamp(t, 0.f, 1.f);

    const Vec2 a1 = lerpKnot(p0, p1, t0, t1, tt);
    const Vec2 a2 = lerpKnot(p1, p2, t1, t2, tt);
    const Vec2 a3 = lerpKnot(p2, p3, t2, t3, tt);
    const Vec2 b1 = lerpKnot(a1, a2, t0, t2, tt);
    const Vec2 b2 = lerpKnot(a2, a3, t1, t3, tt);
    const Vec2 curved = lerpKnot(b1, b2, t1, t2, tt);
    const float amount = std::clamp(curve, 0.f, 1.f);
    return lerp(lerp(p1, p2, t), curved, amount);
}

Vec2 atNode(const NodePool& pool, const std::vector<NodeId>& nodes, int index, bool closed) {
    const int n = static_cast<int>(nodes.size());
    if (n == 0) {
        return {};
    }
    if (closed) {
        const int i = (index % n + n) % n;
        return pool.get(nodes[static_cast<std::size_t>(i)]);
    }
    const int i = std::clamp(index, 0, n - 1);
    return pool.get(nodes[static_cast<std::size_t>(i)]);
}

void appendSpan(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float alpha, float curve,
                float chordError) {
    struct Frame {
        float a;
        float b;
        int depth;
    };
    std::vector<Frame> stack;
    stack.push_back({0.f, 1.f, 0});
    const float error = std::max(chordError, 0.05f);
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        const Vec2 pa = catmull(p0, p1, p2, p3, frame.a, alpha, curve);
        const Vec2 pb = catmull(p0, p1, p2, p3, frame.b, alpha, curve);
        const Vec2 pm = catmull(p0, p1, p2, p3, (frame.a + frame.b) * 0.5f, alpha, curve);
        if (frame.depth >= 10 || pointSegmentDistance(pm, pa, pb) <= error || distance(pa, pb) <= error) {
            if (out.empty() || distance(out.back(), pb) > 1e-3f) {
                out.push_back(pb);
            }
        } else {
            const float mid = (frame.a + frame.b) * 0.5f;
            stack.push_back({mid, frame.b, frame.depth + 1});
            stack.push_back({frame.a, mid, frame.depth + 1});
        }
    }
}

}  // namespace

void collectIds(const Polyline& line, std::vector<NodeId>& out) {
    out.insert(out.end(), line.nodes.begin(), line.nodes.end());
}
void collectIds(const CatmullRom& curve, std::vector<NodeId>& out) {
    out.insert(out.end(), curve.nodes.begin(), curve.nodes.end());
}
void collectIds(const Circle& circle, std::vector<NodeId>& out) { out.push_back(circle.center); }
void collectIds(const Polygon& polygon, std::vector<NodeId>& out) {
    out.insert(out.end(), polygon.nodes.begin(), polygon.nodes.end());
}

Polyline2 resolve(const NodePool& pool, const Polyline& line) {
    Polyline2 out;
    out.closed = line.closed;
    out.pts.reserve(line.nodes.size());
    for (NodeId id : line.nodes) {
        out.pts.push_back(pool.get(id));
    }
    return out;
}

Polyline2 sample(const NodePool& pool, const CatmullRom& curve, float chordError) {
    Polyline2 out;
    out.closed = curve.closed;
    const int n = static_cast<int>(curve.nodes.size());
    if (n == 0) {
        return out;
    }
    if (n == 1) {
        out.pts.push_back(pool.get(curve.nodes[0]));
        return out;
    }
    const float alpha = alphaOf(curve.parameterization);
    const int spans = curve.closed ? n : n - 1;
    out.pts.push_back(atNode(pool, curve.nodes, 0, curve.closed));
    for (int i = 0; i < spans; ++i) {
        const Vec2 p0 = atNode(pool, curve.nodes, i - 1, curve.closed);
        const Vec2 p1 = atNode(pool, curve.nodes, i, curve.closed);
        const Vec2 p2 = atNode(pool, curve.nodes, i + 1, curve.closed);
        const Vec2 p3 = atNode(pool, curve.nodes, i + 2, curve.closed);
        appendSpan(out.pts, p0, p1, p2, p3, alpha, curve.curve, chordError);
    }
    if (curve.closed && out.pts.size() >= 2 && distance(out.pts.front(), out.pts.back()) < 1e-2f) {
        out.pts.pop_back();
    }
    return out;
}

Polygon2 sample(const NodePool& pool, const Circle& circle, float chordError) {
    Polygon2 out;
    const Vec2 center = pool.get(circle.center);
    const float radius = std::max(circle.radius, 0.f);
    if (radius <= 0.f) {
        return out;
    }
    const float error = std::clamp(chordError, 0.05f, radius * 0.5f);
    const float cosHalf = std::clamp(1.f - error / radius, -1.f, 1.f);
    const float step = std::max(2.f * std::acos(cosHalf), 0.05f);
    int count = static_cast<int>(std::ceil((2.f * kPi) / step));
    count = std::clamp(count, 8, 256);
    out.outer.pts.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        // Negative angles walk counter-clockwise on a Y-down screen.
        const float a = -static_cast<float>(i) * (2.f * kPi / static_cast<float>(count));
        out.outer.pts.push_back(center + Vec2{std::cos(a) * radius, std::sin(a) * radius});
    }
    return out;
}

Polygon2 resolve(const NodePool& pool, const Polygon& polygon) {
    Polygon2 out;
    out.outer.pts.reserve(polygon.nodes.size());
    for (NodeId id : polygon.nodes) {
        out.outer.pts.push_back(pool.get(id));
    }
    return out;
}

}  // namespace forma
