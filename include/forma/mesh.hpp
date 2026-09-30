#pragma once

#include "forma/geometry.hpp"

#include <span>
#include <vector>

namespace forma {

struct TriMesh {
    struct Vertex {
        Vec2 position{};
        Color color{};
    };
    std::vector<Vertex> vertices;  // triples of triangles

    void clear() { vertices.clear(); }
    bool empty() const { return vertices.empty(); }
    void add(Vec2 a, Vec2 b, Vec2 c, Color color);
    void append(const TriMesh& other);
};

TriMesh fillConvex(std::span<const Vec2> ring, Color color);
TriMesh fillPolygon(const Polygon2& polygon, Color color);
TriMesh fillPolygons(std::span<const Polygon2> polygons, Color color);
TriMesh strokePolyline(const Polyline2& line, const StrokeStyle& style);

}  // namespace forma
