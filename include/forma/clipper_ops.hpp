#pragma once

#include "forma/mesh.hpp"

#include <span>
#include <vector>

namespace forma {

/// Boolean and offset operations on baked polygons.
/// Curves are sampled before they get here. Results are baked: a union is not a spline.
std::vector<Polygon2> unite(std::span<const Polygon2> shapes);
std::vector<Polygon2> difference(const Polygon2& subject, const Polygon2& clip);
std::vector<Polygon2> intersect(const Polygon2& a, const Polygon2& b);
std::vector<Polygon2> inflate(std::span<const Polygon2> shapes, float delta);

/// Triangulate possibly-holed polygons. Falls back to the ear clipper if Clipper2 refuses the input.
TriMesh triangulate(std::span<const Polygon2> shapes, Color color);

}  // namespace forma
