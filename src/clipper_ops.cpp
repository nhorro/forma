#include "forma/clipper_ops.hpp"

#include "forma/predicates.hpp"

#include "clipper2/clipper.h"
#include "clipper2/clipper.triangulation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace forma {
namespace {

Clipper2Lib::PathD toClipper(std::span<const Vec2> ring) {
    Clipper2Lib::PathD path;
    path.reserve(ring.size());
    for (Vec2 p : ring) {
        const double x = static_cast<double>(p.x);
        const double y = -static_cast<double>(p.y);
        if (!path.empty()) {
            const double dx = path.back().x - x;
            const double dy = path.back().y - y;
            if (dx * dx + dy * dy < 1e-8) {
                continue;
            }
        }
        path.emplace_back(x, y);
    }
    if (path.size() >= 2) {
        const double dx = path.front().x - path.back().x;
        const double dy = path.front().y - path.back().y;
        if (dx * dx + dy * dy < 1e-8) {
            path.pop_back();
        }
    }
    return path;
}

Clipper2Lib::PathsD toClipper(std::span<const Polygon2> shapes) {
    Clipper2Lib::PathsD paths;
    for (const Polygon2& shape : shapes) {
        if (shape.outer.pts.size() >= 3) {
            std::vector<Vec2> outer(shape.outer.pts.begin(), shape.outer.pts.end());
            if (signedArea(outer) < 0.f) {
                std::reverse(outer.begin(), outer.end());
            }
            paths.push_back(toClipper(outer));
        }
        for (const Contour& hole : shape.holes) {
            if (hole.pts.size() >= 3) {
                std::vector<Vec2> ring(hole.pts.begin(), hole.pts.end());
                if (signedArea(ring) > 0.f) {
                    std::reverse(ring.begin(), ring.end());
                }
                paths.push_back(toClipper(ring));
            }
        }
    }
    return paths;
}

Clipper2Lib::PathsD toClipper(const Polygon2& shape) {
    const Polygon2 shapes[] = {shape};
    return toClipper(std::span<const Polygon2>{shapes, 1});
}

std::vector<Polygon2> fromClipper(const Clipper2Lib::PathsD& paths) {
    struct Ring {
        std::vector<Vec2> pts;
        float area = 0.f;
    };
    std::vector<Ring> rings;
    rings.reserve(paths.size());
    for (const Clipper2Lib::PathD& path : paths) {
        Ring ring;
        ring.pts.reserve(path.size());
        for (const Clipper2Lib::PointD& p : path) {
            ring.pts.push_back(Vec2{static_cast<float>(p.x), static_cast<float>(-p.y)});
        }
        ring.area = signedArea(ring.pts);
        if (ring.pts.size() >= 3 && std::fabs(ring.area) > 1e-2f) {
            rings.push_back(std::move(ring));
        }
    }

    std::vector<Polygon2> outers;
    std::vector<Ring> holes;
    for (Ring& ring : rings) {
        if (ring.area >= 0.f) {
            Polygon2 poly;
            poly.outer.pts = std::move(ring.pts);
            outers.push_back(std::move(poly));
        } else {
            holes.push_back(std::move(ring));
        }
    }
    for (Ring& hole : holes) {
        int best = -1;
        float bestArea = 1e30f;
        if (hole.pts.empty()) {
            continue;
        }
        for (int i = 0; i < static_cast<int>(outers.size()); ++i) {
            const float area = std::fabs(signedArea(outers[static_cast<std::size_t>(i)].outer.pts));
            if (area < bestArea && pointInPolygon(hole.pts.front(), outers[static_cast<std::size_t>(i)].outer.pts)) {
                best = i;
                bestArea = area;
            }
        }
        if (best >= 0) {
            outers[static_cast<std::size_t>(best)].holes.push_back(Contour{std::move(hole.pts)});
        }
    }
    return outers;
}

}  // namespace

std::vector<Polygon2> unite(std::span<const Polygon2> shapes) {
    const Clipper2Lib::PathsD subject = toClipper(shapes);
    if (subject.empty()) {
        return {};
    }
    const Clipper2Lib::PathsD solution =
        Clipper2Lib::Union(subject, Clipper2Lib::FillRule::NonZero, 2);
    return fromClipper(solution);
}

std::vector<Polygon2> difference(const Polygon2& subject, const Polygon2& clip) {
    const Clipper2Lib::PathsD solution = Clipper2Lib::Difference(
        toClipper(subject), toClipper(clip), Clipper2Lib::FillRule::NonZero, 2);
    return fromClipper(solution);
}

std::vector<Polygon2> intersect(const Polygon2& a, const Polygon2& b) {
    const Clipper2Lib::PathsD solution =
        Clipper2Lib::Intersect(toClipper(a), toClipper(b), Clipper2Lib::FillRule::NonZero, 2);
    return fromClipper(solution);
}

std::vector<Polygon2> inflate(std::span<const Polygon2> shapes, float delta) {
    const Clipper2Lib::PathsD subject = toClipper(shapes);
    if (subject.empty() || delta == 0.f) {
        return std::vector<Polygon2>(shapes.begin(), shapes.end());
    }
    const Clipper2Lib::PathsD solution = Clipper2Lib::InflatePaths(
        subject, static_cast<double>(delta), Clipper2Lib::JoinType::Miter, Clipper2Lib::EndType::Polygon);
    return fromClipper(solution);
}

TriMesh triangulate(std::span<const Polygon2> shapes, Color color) {
    TriMesh mesh;
    const Clipper2Lib::PathsD subject = toClipper(shapes);
    if (!subject.empty()) {
        Clipper2Lib::PathsD triangles;
        const Clipper2Lib::TriangulateResult result =
            Clipper2Lib::Triangulate(subject, 2, triangles, false);
        if (result == Clipper2Lib::TriangulateResult::success) {
            for (const Clipper2Lib::PathD& tri : triangles) {
                if (tri.size() < 3) {
                    continue;
                }
                const Vec2 a{static_cast<float>(tri[0].x), static_cast<float>(-tri[0].y)};
                const Vec2 b{static_cast<float>(tri[1].x), static_cast<float>(-tri[1].y)};
                const Vec2 c{static_cast<float>(tri[2].x), static_cast<float>(-tri[2].y)};
                mesh.add(a, b, c, color);
            }
            if (!mesh.empty()) {
                return mesh;
            }
        }
    }
    return fillPolygons(shapes, color);
}

}  // namespace forma
