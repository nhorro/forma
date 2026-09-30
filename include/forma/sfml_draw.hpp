#pragma once

#include "forma/mesh.hpp"

#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/VertexArray.hpp>

namespace forma {

sf::VertexArray toVertexArray(const TriMesh& mesh);
void draw(sf::RenderTarget& target, const TriMesh& mesh);
void draw(sf::RenderTarget& target, const sf::VertexArray& vertices);

}  // namespace forma
