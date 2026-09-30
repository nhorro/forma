#include "forma/mesh.hpp"

#include "forma/predicates.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace forma {
namespace {

void dedupClose(std::vector<Vec2>& pts) {
    if (pts.size() >= 2 && distance(pts.front(), pts.back()) < 1e-3f) {
        pts.pop_back();
    }
}

std::vector<Vec2> orientedScreenCcw(std::span<const Vec2> ring) {
    std::vector<Vec2> pts(ring.begin(), ring.end());
    dedupClose(pts);
    if (pts.size() >= 3 && signedArea(pts) < 0.f) {
        std::reverse(pts.begin(), pts.end());
    }
    return pts;
}

bool pointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    const float c1 = screenCross(a, b, p);
    const float c2 = screenCross(b, c, p);
    const float c3 = screenCross(c, a, p);
    const bool hasNeg = (c1 < -1e-5f) || (c2 < -1e-5f) || (c3 < -1e-5f);
    const bool hasPos = (c1 > 1e-5f) || (c2 > 1e-5f) || (c3 > 1e-5f);
    return !(hasNeg && hasPos);
}

void addArc(TriMesh& mesh, Vec2 center, Vec2 from, Vec2 to, Color color, int steps) {
    if (lengthSq(from) < 1e-8f || lengthSq(to) < 1e-8f) {
        return;
    }
    const float a0 = std::atan2(from.y, from.x);
    float a1 = std::atan2(to.y, to.x);
    float sweep = a1 - a0;
    while (sweep <= -3.14159265f) {
        sweep += 6.2831853f;
    }
    while (sweep > 3.14159265f) {
        sweep -= 6.2831853f;
    }
    // The join we want is the shorter exterior fan, which may be either way.
    // Caller passes vectors already on the outer side; take the shorter sweep.
    if (sweep > 3.14159265f) {
        sweep -= 6.2831853f;
    }
    const float radius = length(from);
    Vec2 prev = center + from;
    const int n = std::max(steps, 1);
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float a = a0 + sweep * t;
        const Vec2 next = center + Vec2{std::cos(a), std::sin(a)} * radius;
        mesh.add(center, prev, next, color);
        prev = next;
    }
}

Vec2 leftNormal(Vec2 dir) {
    const Vec2 n = normalized(dir);
    return {-n.y, n.x};
}

}  // namespace

void TriMesh::add(Vec2 a, Vec2 b, Vec2 c, Color color) {
    vertices.push_back({a, color});
    vertices.push_back({b, color});
    vertices.push_back({c, color});
}

void TriMesh::append(const TriMesh& other) { vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end()); }

TriMesh fillConvex(std::span<const Vec2> ring, Color color) {
    TriMesh mesh;
    std::vector<Vec2> pts = orientedScreenCcw(ring);
    if (pts.size() < 3) {
        return mesh;
    }
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        mesh.add(pts[0], pts[i], pts[i + 1], color);
    }
    return mesh;
}

TriMesh fillPolygon(const Polygon2& polygon, Color color) {
    // Holes are not ear-clipped here. Callers with holes should triangulate via Clipper2.
    if (!polygon.holes.empty()) {
        return {};
    }
    std::vector<Vec2> pts = orientedScreenCcw(polygon.outer.pts);
    if (pts.size() < 3) {
        return {};
    }
    if (isConvex(pts)) {
        return fillConvex(pts, color);
    }

    TriMesh mesh;
    std::vector<int> idx(pts.size());
    for (std::size_t i = 0; i < idx.size(); ++i) {
        idx[i] = static_cast<int>(i);
    }
    int guard = static_cast<int>(pts.size() * pts.size());
    while (idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const int i0 = idx[(i + idx.size() - 1) % idx.size()];
            const int i1 = idx[i];
            const int i2 = idx[(i + 1) % idx.size()];
            if (screenCross(pts[static_cast<std::size_t>(i0)], pts[static_cast<std::size_t>(i1)],
                            pts[static_cast<std::size_t>(i2)]) <= 1e-5f) {
                continue;
            }
            bool ear = true;
            for (int other : idx) {
                if (other == i0 || other == i1 || other == i2) {
                    continue;
                }
                if (pointInTri(pts[static_cast<std::size_t>(other)], pts[static_cast<std::size_t>(i0)],
                               pts[static_cast<std::size_t>(i1)], pts[static_cast<std::size_t>(i2)])) {
                    ear = false;
                    break;
                }
            }
            if (!ear) {
                continue;
            }
            mesh.add(pts[static_cast<std::size_t>(i0)], pts[static_cast<std::size_t>(i1)],
                     pts[static_cast<std::size_t>(i2)], color);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            break;
        }
    }
    if (idx.size() == 3) {
        mesh.add(pts[static_cast<std::size_t>(idx[0])], pts[static_cast<std::size_t>(idx[1])],
                 pts[static_cast<std::size_t>(idx[2])], color);
    } else if (idx.size() > 3 && mesh.empty()) {
        return fillConvex(pts, color);
    }
    return mesh;
}

TriMesh fillPolygons(std::span<const Polygon2> polygons, Color color) {
    TriMesh mesh;
    for (const Polygon2& polygon : polygons) {
        mesh.append(fillPolygon(polygon, color));
    }
    return mesh;
}

TriMesh strokePolyline(const Polyline2& line, const StrokeStyle& style) {
    TriMesh mesh;
    if (style.width <= 0.f || line.pts.size() < 2) {
        return mesh;
    }
    std::vector<Vec2> pts = line.pts;
    if (line.closed) {
        dedupClose(pts);
    }
    if (pts.size() < 2) {
        return mesh;
    }
    const float half = style.width * 0.5f;
    const std::size_t n = pts.size();
    const std::size_t segCount = line.closed ? n : n - 1;

    std::vector<Vec2> dirs(segCount);
    std::vector<Vec2> normals(segCount);
    for (std::size_t i = 0; i < segCount; ++i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % n];
        Vec2 d = b - a;
        if (lengthSq(d) < 1e-10f) {
            d = {1.f, 0.f};
        }
        dirs[i] = normalized(d);
        normals[i] = leftNormal(dirs[i]);
    }

    auto emitSegment = [&](std::size_t i) {
        const Vec2 a = pts[i];
        const Vec2 b = pts[(i + 1) % n];
        const Vec2 off = normals[i] * half;
        mesh.add(a + off, a - off, b - off, style.color);
        mesh.add(a + off, b - off, b + off, style.color);
    };
    for (std::size_t i = 0; i < segCount; ++i) {
        emitSegment(i);
    }

    auto emitJoin = [&](std::size_t vertex, std::size_t prevSeg, std::size_t nextSeg) {
        const Vec2 n0 = normals[prevSeg];
        const Vec2 n1 = normals[nextSeg];
        if (dot(n0, n1) > 0.995f) {
            return;
        }
        const float turn = cross(dirs[prevSeg], dirs[nextSeg]);
        const Vec2 center = pts[vertex];
        // Outer side is the left normal when the math turn is left (turn > 0 in Y-up).
        const float side = turn >= 0.f ? 1.f : -1.f;
        if (style.join == LineJoin::Bevel) {
            mesh.add(center, center + n0 * half * side, center + n1 * half * side, style.color);
            return;
        }
        const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(turn) / 0.45f)), 2, 8);
        addArc(mesh, center, n0 * half * side, n1 * half * side, style.color, steps);
    };

    if (line.closed) {
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t prev = (i + segCount - 1) % segCount;
            emitJoin(i, prev, i % segCount);
        }
    } else {
        for (std::size_t i = 1; i + 1 < n; ++i) {
            emitJoin(i, i - 1, i);
        }
        if (style.cap == LineCap::Round) {
            const int steps = 6;
            addArc(mesh, pts.front(), normals.front() * half, -normals.front() * half, style.color, steps);
            addArc(mesh, pts.back(), -normals.back() * half, normals.back() * half, style.color, steps);
        }
    }
    return mesh;
}

}  // namespace forma
