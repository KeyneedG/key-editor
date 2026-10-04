#pragma once

#include "tiny3d/engine.hpp"
#include <functional>

namespace tiny3d {
namespace detail {
struct UIClipCache {
    std::shared_ptr<const Mesh> source, result;
    std::vector<float> state;
};
}

struct Canvas : Component {
    Entity* camera = nullptr; // The camera used to render and pick this world-space UI.
};

struct Image : Component {
    Color color{48, 58, 76};
    float width = 100, height = 30;
private:
    friend class UI;
    float builtWidth_ = -1, builtHeight_ = -1;
    std::shared_ptr<const Mesh> sourceMesh_;
    detail::UIClipCache clip_;
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
    std::shared_ptr<const Mesh> sourceMesh_;
    detail::UIClipCache clip_;
};

struct Button : Component {
    bool interactable = true;
    bool hovered = false, pressed = false; // Updated by UI.
    Color normalColor{54, 75, 107}, hoverColor{72, 102, 146};
    Color pressedColor{38, 56, 84}, disabledColor{55, 59, 67};
    std::function<void()> onClick; // Press and release on the same button.
};

// Attach to an Image. Descendant graphics and picking are clipped to its rectangle.
// Content is a direct child; offset moves it upwards from its initial local position.
struct ScrollRect : Component {
    Entity* content = nullptr;
    float contentHeight = 0, offset = 0, wheelSpeed = 28;
    Vector3 contentPosition() const { return trackedContent_ == content ? origin_ : content ? content->transform.localPosition : Vector3{}; }
private:
    friend class UI;
    Entity* trackedContent_ = nullptr;
    Vector3 origin_{};
};

// Attach to an Image. Enter or focus loss commits, Escape restores the initial text.
struct InputField : Component {
    std::string text, placeholder;
    bool interactable = true, multiline = false;
    std::size_t maxLength = 4096;
    float pixelSize = 1.5f;
    std::function<void(const std::string&)> onSubmit;
    bool focused = false;
private:
    friend class UI;
    MeshRenderer* textVisual_ = nullptr;
    std::string builtText_;
    float builtPixelSize_ = 0, builtWidth_ = 0, builtHeight_ = 0;
    std::shared_ptr<const Mesh> sourceMesh_;
    detail::UIClipCache clip_;
};

class UI {
public:
    void update(Scene& scene, const Input& input);
    void cancel();
    bool editingText() const { return focused_ != nullptr; }
    Entity* focusedEntity() const { return focused_; }
    void focus(Entity& entity);
    // Rebuild changed graphics without processing input/clicks again.
    void refresh(Scene& scene);
private:
    Entity* pressed_ = nullptr;
    Entity* focused_ = nullptr;
    std::string initialText_;
    std::size_t cursor_ = 0;
    bool selectAll_ = false;
};

} // namespace tiny3d
