#include "imgui_sfml.hpp"

#include <imgui.h>

#include <SFML/Graphics/RenderStates.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/Window/Mouse.hpp>

#include <cstdint>

namespace forma_ui {
namespace {

ImGuiKey mapKey(sf::Keyboard::Key key) {
    switch (key) {
        case sf::Keyboard::Key::Tab: return ImGuiKey_Tab;
        case sf::Keyboard::Key::Left: return ImGuiKey_LeftArrow;
        case sf::Keyboard::Key::Right: return ImGuiKey_RightArrow;
        case sf::Keyboard::Key::Up: return ImGuiKey_UpArrow;
        case sf::Keyboard::Key::Down: return ImGuiKey_DownArrow;
        case sf::Keyboard::Key::PageUp: return ImGuiKey_PageUp;
        case sf::Keyboard::Key::PageDown: return ImGuiKey_PageDown;
        case sf::Keyboard::Key::Home: return ImGuiKey_Home;
        case sf::Keyboard::Key::End: return ImGuiKey_End;
        case sf::Keyboard::Key::Insert: return ImGuiKey_Insert;
        case sf::Keyboard::Key::Delete: return ImGuiKey_Delete;
        case sf::Keyboard::Key::Backspace: return ImGuiKey_Backspace;
        case sf::Keyboard::Key::Enter: return ImGuiKey_Enter;
        case sf::Keyboard::Key::Escape: return ImGuiKey_Escape;
        case sf::Keyboard::Key::Space: return ImGuiKey_Space;
        case sf::Keyboard::Key::A: return ImGuiKey_A;
        case sf::Keyboard::Key::C: return ImGuiKey_C;
        case sf::Keyboard::Key::V: return ImGuiKey_V;
        case sf::Keyboard::Key::X: return ImGuiKey_X;
        case sf::Keyboard::Key::Y: return ImGuiKey_Y;
        case sf::Keyboard::Key::Z: return ImGuiKey_Z;
        default: return ImGuiKey_None;
    }
}

void modifiers(const sf::Event::KeyPressed& key) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, key.control);
    io.AddKeyEvent(ImGuiMod_Shift, key.shift);
    io.AddKeyEvent(ImGuiMod_Alt, key.alt);
    io.AddKeyEvent(ImGuiMod_Super, key.system);
}

}  // namespace

bool ImGuiLayer::init() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    fontOk_ = font_.resize({static_cast<unsigned>(width), static_cast<unsigned>(height)});
    if (fontOk_) {
        font_.update(pixels);
        font_.setSmooth(false);
    }
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.f;
    style.FrameRounding = 2.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.867f, 0.831f, 0.769f, 0.96f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.110f, 0.098f, 0.082f, 1.f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.957f, 0.937f, 0.902f, 1.f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.910f, 0.870f, 0.800f, 1.f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.769f, 0.286f, 0.114f, 0.85f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.769f, 0.286f, 0.114f, 1.f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.769f, 0.286f, 0.114f, 0.28f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.769f, 0.286f, 0.114f, 0.45f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.769f, 0.286f, 0.114f, 1.f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.769f, 0.286f, 0.114f, 1.f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.110f, 0.098f, 0.082f, 1.f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.110f, 0.098f, 0.082f, 1.f);
    return fontOk_;
}

void ImGuiLayer::process(const sf::Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        modifiers(*key);
        const ImGuiKey mapped = mapKey(key->code);
        if (mapped != ImGuiKey_None) {
            io.AddKeyEvent(mapped, true);
        }
    } else if (const auto* key = event.getIf<sf::Event::KeyReleased>()) {
        io.AddKeyEvent(ImGuiMod_Ctrl, key->control);
        io.AddKeyEvent(ImGuiMod_Shift, key->shift);
        io.AddKeyEvent(ImGuiMod_Alt, key->alt);
        const ImGuiKey mapped = mapKey(key->code);
        if (mapped != ImGuiKey_None) {
            io.AddKeyEvent(mapped, false);
        }
    } else if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
        if (text->unicode >= 32) {
            io.AddInputCharacter(text->unicode);
        }
    } else if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        wheel_ += wheel->delta;
    } else if (const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (pressed->button == sf::Mouse::Button::Left) {
            io.AddMouseButtonEvent(0, true);
        } else if (pressed->button == sf::Mouse::Button::Right) {
            io.AddMouseButtonEvent(1, true);
        } else if (pressed->button == sf::Mouse::Button::Middle) {
            io.AddMouseButtonEvent(2, true);
        }
    } else if (const auto* released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (released->button == sf::Mouse::Button::Left) {
            io.AddMouseButtonEvent(0, false);
        } else if (released->button == sf::Mouse::Button::Right) {
            io.AddMouseButtonEvent(1, false);
        } else if (released->button == sf::Mouse::Button::Middle) {
            io.AddMouseButtonEvent(2, false);
        }
    } else if (event.is<sf::Event::FocusGained>()) {
        io.AddFocusEvent(true);
    } else if (event.is<sf::Event::FocusLost>()) {
        io.AddFocusEvent(false);
    }
}

void ImGuiLayer::begin(const sf::RenderWindow& window, float dt) {
    ImGuiIO& io = ImGui::GetIO();
    const sf::Vector2u size = window.getSize();
    io.DisplaySize = ImVec2(static_cast<float>(size.x), static_cast<float>(size.y));
    io.DeltaTime = dt > 0.f ? dt : 1.f / 60.f;
    const sf::Vector2i mouse = sf::Mouse::getPosition(window);
    io.AddMousePosEvent(static_cast<float>(mouse.x), static_cast<float>(mouse.y));
    if (wheel_ != 0.f) {
        io.AddMouseWheelEvent(0.f, wheel_);
        wheel_ = 0.f;
    }
    ImGui::NewFrame();
}

void ImGuiLayer::draw(sf::RenderWindow& window) const {
    ImGui::Render();
    ImDrawData* data = ImGui::GetDrawData();
    if (!data || !fontOk_) {
        return;
    }
    const sf::Vector2u size = window.getSize();
    const float width = static_cast<float>(size.x);
    const float height = static_cast<float>(size.y);
    if (width < 1.f || height < 1.f) {
        return;
    }
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList* list = data->CmdLists[listIndex];
        for (int cmdIndex = 0; cmdIndex < list->CmdBuffer.Size; ++cmdIndex) {
            const ImDrawCmd& cmd = list->CmdBuffer[cmdIndex];
            if (cmd.UserCallback || cmd.ElemCount == 0) {
                continue;
            }
            const ImVec2 clipMin{cmd.ClipRect.x, cmd.ClipRect.y};
            const ImVec2 clipMax{cmd.ClipRect.z, cmd.ClipRect.w};
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
                continue;
            }
            sf::VertexArray triangles(sf::PrimitiveType::Triangles, cmd.ElemCount);
            for (unsigned i = 0; i < cmd.ElemCount; ++i) {
                const ImDrawVert& vertex = list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset + i]];
                sf::Vertex& out = triangles[i];
                out.position = {vertex.pos.x, vertex.pos.y};
                out.texCoords = {vertex.uv.x, vertex.uv.y};
                const ImU32 col = vertex.col;
                out.color = sf::Color(static_cast<std::uint8_t>(col & 0xff), static_cast<std::uint8_t>((col >> 8) & 0xff),
                                      static_cast<std::uint8_t>((col >> 16) & 0xff), static_cast<std::uint8_t>((col >> 24) & 0xff));
            }
            sf::View view = window.getDefaultView();
            view.setScissor(sf::FloatRect({clipMin.x / width, clipMin.y / height},
                                          {(clipMax.x - clipMin.x) / width, (clipMax.y - clipMin.y) / height}));
            window.setView(view);
            sf::RenderStates states;
            states.texture = &font_;
            states.coordinateType = sf::CoordinateType::Normalized;
            window.draw(triangles, states);
        }
    }
    window.setView(window.getDefaultView());
}

}  // namespace forma_ui
