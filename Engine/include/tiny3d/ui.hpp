#pragma once

#include "tiny3d/engine.hpp"
#include <functional>

namespace tiny3d {

struct Canvas : Component {
    Entity* camera = nullptr; // The camera used to render and pick this world-space UI.
};

struct Image : Component {
    Color color{48, 58, 76};
    float width = 100, height = 30;
private:
    friend class UI;
    float builtWidth_ = -1, builtHeight_ = -1;
};

struct SimpleText : Component {
    std::string text;
    Color color{240, 244, 255};
    float pixelSize = 2; // Each ink pixel is a small XY plane, in local units.
    bool centered = true;
private:
    friend class UI;
    std::string builtText_;
    float builtPixelSize_ = -1;
    bool builtCentered_ = true;
};

struct Button : Component {
    bool interactable = true;
    bool hovered = false, pressed = false; // Updated by UI.
    Color normalColor{54, 75, 107}, hoverColor{72, 102, 146};
    Color pressedColor{38, 56, 84}, disabledColor{55, 59, 67};
    std::function<void()> onClick; // Press and release on the same button.
};

class UI {
public:
    void update(Scene& scene, const Input& input);
    void cancel() { pressed_ = nullptr; }
private:
    void refresh(Scene& scene);
    Entity* pressed_ = nullptr;
};

} // namespace tiny3d
