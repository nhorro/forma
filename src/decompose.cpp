#include "forma/decompose.hpp"

#include "forma/predicates.hpp"

#include <algorithm>
#include <cmath>

namespace forma {
namespace {

constexpr float kEps = 1e-4f;
constexpr int kMaxVerts = 8;

std::vector<Vec2> cleaned(std::span<const Vec2> ring) {
    std::vector<Vec2> pts;
    pts.reserve(ring.size());
    for (Vec2 p : ring) {
        if (!pts.empty() && distance(pts.back(), p) < kEps) {
            continue;
        }
        pts.push_back(p);
    }
    if (pts.size() >= 2 && distance(pts.front(), pts.back()) < kEps) {
        pts.pop_back();
    }
    if (pts.size() >= 3 && !isScreenCounterClockwise(pts)) {
        std::reverse(pts.begin(), pts.end());
    }
    return pts;
}

bool reflex(const std::vector<Vec2>& pts, int index) {
    const int n = static_cast<int>(pts.size());
    const Vec2 prev = pts[static_cast<std::size_t>((index + n - 1) % n)];
    const Vec2 curr = pts[static_cast<std::size_t>(index)];
    const Vec2 next = pts[static_cast<std::size_t>((index + 1) % n)];
    return screenCross(prev, curr, next) < -kEps;
}

bool shares(int edgeA, int edgeB, int i, int j) {
    return edgeA == i || edgeA == j || edgeB == i || edgeB == j;
}

bool diagonal(const std::vector<Vec2>& pts, int i, int j) {
    const int n = static_cast<int>(pts.size());
    if (i == j || (i + 1) % n == j || (j + 1) % n == i) {
        return false;
    }
    const Vec2 a = pts[static_cast<std::size_t>(i)];
    const Vec2 b = pts[static_cast<std::size_t>(j)];
    for (int e = 0; e < n; ++e) {
        const int f = (e + 1) % n;
        if (shares(e, f, i, j)) {
            continue;
        }
        if (segmentsIntersect(a, b, pts[static_cast<std::size_t>(e)], pts[static_cast<std::size_t>(f)])) {
            return false;
        }
    }
    return pointInPolygon((a + b) * 0.5f, pts);
}

std::vector<Vec2> slice(const std::vector<Vec2>& pts, int from, int to) {
    std::vector<Vec2> out;
    const int n = static_cast<int>(pts.size());
    for (int k = from;; k = (k + 1) % n) {
        out.push_back(pts[static_cast<std::size_t>(k)]);
        if (k == to) {
            break;
        }
        if (out.size() > pts.size()) {
            break;
        }
    }
    return out;
}

void split(std::vector<Vec2> pts, std::vector<std::vector<Vec2>>& out, int depth) {
    if (pts.size() < 3 || depth > 64) {
        return;
    }
    if (isConvex(pts) && static_cast<int>(pts.size()) <= kMaxVerts) {
        out.push_back(std::move(pts));
        return;
    }
    const int n = static_cast<int>(pts.size());
    int pivot = -1;
    for (int i = 0; i < n; ++i) {
        if (reflex(pts, i)) {
            pivot = i;
            break;
        }
    }
    if (pivot < 0) {
        pivot = 0;
    }
    int other = -1;
    float best = 1e30f;
    for (int j = 0; j < n; ++j) {
        if (!diagonal(pts, pivot, j)) {
            continue;
        }
        const auto left = slice(pts, pivot, j);
        const auto right = slice(pts, j, pivot);
        if (left.size() < 3 || right.size() < 3) {
            continue;
        }
        const float span = distance(pts[static_cast<std::size_t>(pivot)], pts[static_cast<std::size_t>(j)]);
        if (span < best) {
            best = span;
            other = j;
        }
    }
    if (other < 0) {
        std::vector<Vec2> hull = convexHull(pts);
        if (hull.size() >= 3) {
            if (static_cast<int>(hull.size()) > kMaxVerts) {
                hull.resize(static_cast<std::size_t>(kMaxVerts));
            }
            out.push_back(std::move(hull));
        }
        return;
    }
    split(slice(pts, pivot, other), out, depth + 1);
    split(slice(pts, other, pivot), out, depth + 1);
}

}  // namespace

std::vector<std::vector<Vec2>> convexParts(std::span<const Vec2> ring) {
    std::vector<std::vector<Vec2>> parts;
    split(cleaned(ring), parts, 0);
    return parts;
}

}  // namespace forma
