#include "forma/sfml_draw.hpp"

namespace forma {

sf::VertexArray toVertexArray(const TriMesh& mesh) {
    sf::VertexArray vertices(sf::PrimitiveType::Triangles, mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const TriMesh::Vertex& src = mesh.vertices[i];
        vertices[i].position = {src.position.x, src.position.y};
        vertices[i].color = sf::Color{src.color.r, src.color.g, src.color.b, src.color.a};
    }
    return vertices;
}

void draw(sf::RenderTarget& target, const sf::VertexArray& vertices) { target.draw(vertices); }

void draw(sf::RenderTarget& target, const TriMesh& mesh) { target.draw(toVertexArray(mesh)); }

}  // namespace forma
