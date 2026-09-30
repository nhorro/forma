#include "forma/predicates.hpp"

#include <algorithm>
#include <cmath>

namespace forma {
namespace {

constexpr float kEps = 1e-5f;

}  // namespace

float signedArea(std::span<const Vec2> pts) {
    const std::size_t n = pts.size();
    if (n < 3) {
        return 0.f;
    }
    float sum = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % n];
        sum += cross(a, b);
    }
    // Negate so counter-clockwise on a Y-down screen is positive.
    return -0.5f * sum;
}

bool isScreenCounterClockwise(std::span<const Vec2> pts) { return signedArea(pts) > 0.f; }

bool pointInPolygon(Vec2 p, std::span<const Vec2> ring) {
    bool inside = false;
    const std::size_t n = ring.size();
    if (n < 3) {
        return false;
    }
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 a = ring[i];
        const Vec2 b = ring[j];
        const bool crosses = ((a.y > p.y) != (b.y > p.y)) &&
                             (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + 1e-12f) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const float d1 = cross(c, d, a);
    const float d2 = cross(c, d, b);
    const float d3 = cross(a, b, c);
    const float d4 = cross(a, b, d);
    if (((d1 > kEps && d2 < -kEps) || (d1 < -kEps && d2 > kEps)) &&
        ((d3 > kEps && d4 < -kEps) || (d3 < -kEps && d4 > kEps))) {
        return true;
    }
    return false;
}

bool isConvex(std::span<const Vec2> pts) {
    const std::size_t n = pts.size();
    if (n < 3) {
        return false;
    }
    int sign = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const float c = screenCross(pts[i], pts[(i + 1) % n], pts[(i + 2) % n]);
        if (std::fabs(c) <= kEps) {
            continue;
        }
        const int s = c > 0.f ? 1 : -1;
        if (sign == 0) {
            sign = s;
        } else if (s != sign) {
            return false;
        }
    }
    return sign != 0;
}

Vec2 polygonCentroid(std::span<const Vec2> pts) {
    const std::size_t n = pts.size();
    if (n == 0) {
        return {};
    }
    if (n < 3) {
        Vec2 sum{};
        for (Vec2 p : pts) {
            sum += p;
        }
        return sum / static_cast<float>(n);
    }
    float acc = 0.f;
    Vec2 center{};
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % n];
        const float cr = cross(a, b);
        acc += cr;
        center.x += (a.x + b.x) * cr;
        center.y += (a.y + b.y) * cr;
    }
    if (std::fabs(acc) < 1e-6f) {
        Vec2 sum{};
        for (Vec2 p : pts) {
            sum += p;
        }
        return sum / static_cast<float>(n);
    }
    return center / (3.f * acc);
}

std::vector<Vec2> convexHull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    pts.erase(std::unique(pts.begin(), pts.end(),
                          [](Vec2 a, Vec2 b) { return distance(a, b) < 1e-4f; }),
              pts.end());
    if (pts.size() <= 1) {
        return pts;
    }
    std::vector<Vec2> lower;
    std::vector<Vec2> upper;
    for (Vec2 p : pts) {
        while (lower.size() >= 2 && cross(lower[lower.size() - 2], lower.back(), p) <= 0.f) {
            lower.pop_back();
        }
        lower.push_back(p);
    }
    for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
        while (upper.size() >= 2 && cross(upper[upper.size() - 2], upper.back(), *it) <= 0.f) {
            upper.pop_back();
        }
        upper.push_back(*it);
    }
    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

}  // namespace forma
