#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Window/Event.hpp>

namespace forma_ui {

class ImGuiLayer {
public:
    bool init();
    void process(const sf::Event& event);
    void begin(const sf::RenderWindow& window, float dt);
    void draw(sf::RenderWindow& window) const;

private:
    sf::Texture font_;
    float wheel_ = 0.f;
    bool fontOk_ = false;
};

}  // namespace forma_ui
