#pragma once

#include "tiny3d/math.hpp"
#include "tiny3d/graphics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <concepts>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tiny3d {

constexpr std::uint32_t layerMask(unsigned layer) { return layer < 32 ? (std::uint32_t{1} << layer) : 0; }

struct Color {
    std::uint8_t r = 255, g = 255, b = 255;
    std::uint32_t packed() const {
        return (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
};

struct Mesh {
    std::vector<Vector3> vertices{};
    // Counterclockwise winding when viewed from outside the surface.
    std::vector<std::array<std::size_t, 3>> triangles{};
    std::uint64_t revision = 0;
    // Call after editing these arrays in place so cached render backends re-upload them.
    void markChanged() { ++revision; }
};

Mesh cube();   // Unit cube, centered at the origin.
Mesh plane();  // Unit square on XZ, facing +Y.

struct Component {
    bool enabled = true;
    virtual ~Component() = 0;
};
inline Component::~Component() = default;

struct MeshRenderer : Component {
    std::shared_ptr<const Mesh> mesh;
    Color color{};
    bool unlit = false;

    explicit MeshRenderer(std::shared_ptr<const Mesh> mesh = {}, Color color = {})
        : mesh(std::move(mesh)), color(color) {}
};

struct Camera : Component {
    float depth = 0; // Cameras render in increasing depth order.
    std::uint32_t layers = ~std::uint32_t{0}; // All 32 layers by default.
    bool clearColor = true;
    bool orthographic = false;
    float orthographicSize = 5; // Half the vertical view size, in world units.
    float fieldOfView = pi / 3; // Vertical field of view in radians.
    float nearPlane = 0.1f;
    float farPlane = 100;
};

struct Entity {
    Transform transform{};
    bool visible = true;
    std::uint8_t layer = 0;
    std::string name = "Object";
    std::string tag = "Untagged";
    std::vector<std::unique_ptr<Component>> components;

    Entity() = default;
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&& entity) noexcept { *this = std::move(entity); }
    Entity& operator=(Entity&& entity) noexcept {
        if (this == &entity) return *this;
        detach();
        transform = entity.transform;
        visible = entity.visible;
        layer = entity.layer;
        name = std::move(entity.name);
        tag = std::move(entity.tag);
        components = std::move(entity.components);
        parent_ = entity.parent_;
        transform.parent_ = parent_ ? &parent_->transform : nullptr;
        children_ = std::move(entity.children_);
        if (parent_) {
            for (Entity*& child : parent_->children_) if (child == &entity) child = this;
        }
        for (Entity* child : children_) {
            child->parent_ = this;
            child->transform.parent_ = &transform;
        }
        entity.parent_ = nullptr;
        entity.transform.parent_ = nullptr;
        entity.children_.clear();
        return *this;
    }
    ~Entity() { detach(); }

    Entity* parent() const { return parent_; }
    std::size_t childCount() const { return children_.size(); }
    Entity* getChild(std::size_t index) { return index < children_.size() ? children_[index] : nullptr; }
    const Entity* getChild(std::size_t index) const { return index < children_.size() ? children_[index] : nullptr; }
    bool visibleInHierarchy() const {
        for (const Entity* entity = this; entity; entity = entity->parent_) if (!entity->visible) return false;
        return true;
    }
    // By default, preserve world position, rotation, and scale magnitudes.
    void setParent(Entity* parent, bool worldPositionStays = true) {
        if (parent == parent_) return;
        for (const Entity* ancestor = parent; ancestor; ancestor = ancestor->parent_) {
            if (ancestor == this) throw std::invalid_argument("Entity parenting cannot form a cycle");
        }
        const Vector3 position = transform.position(), scale = transform.lossyScale();
        const Quaternion rotation = transform.rotation();
        if (worldPositionStays && parent) {
            parent->transform.inversePoint(position); // Validate before changing the hierarchy.
        }
        if (parent) parent->children_.push_back(this);
        if (parent_) {
            auto& children = parent_->children_;
            children.erase(std::remove(children.begin(), children.end(), this), children.end());
        }
        parent_ = parent;
        transform.parent_ = parent ? &parent->transform : nullptr;
        if (worldPositionStays) {
            transform.setPosition(position);
            transform.setRotation(rotation);
            const auto axisScale = [&](Vector3 axis) {
                axis = rotate(axis, transform.localRotation);
                return parent ? length(parent->transform.vector(axis)) : length(axis);
            };
            transform.localScale = {scale.x / axisScale({1, 0, 0}),
                scale.y / axisScale({0, 1, 0}), scale.z / axisScale({0, 0, 1})};
        }
    }

    template<class T, class... Args>
        requires std::derived_from<T, Component> && std::constructible_from<T, Args...>
    T& addComponent(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *component;
        components.push_back(std::move(component));
        return result;
    }

    template<class T>
        requires std::derived_from<T, Component>
    T* getComponent() {
        for (auto& component : components) {
            if (auto* found = dynamic_cast<T*>(component.get())) return found;
        }
        return nullptr;
    }

    template<class T>
        requires std::derived_from<T, Component>
    const T* getComponent() const {
        for (const auto& component : components) {
            if (auto* found = dynamic_cast<const T*>(component.get())) return found;
        }
        return nullptr;
    }

private:
    void detach() {
        while (!children_.empty()) children_.back()->setParent(nullptr);
        if (parent_) setParent(nullptr);
    }
    Entity* parent_ = nullptr;
    std::vector<Entity*> children_;
};

struct Scene {
    // Appending entities does not invalidate parent pointers or entity references.
    std::deque<Entity> entities;
};

class Renderer {
public:
    Renderer(int width, int height);
    void resize(int width, int height);
    void render(const Scene& scene, Color background = {28, 36, 52});
    void savePPM(const std::string& path) const;

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint32_t>& pixels() const { return pixels_; }

private:
    struct ScreenPoint { float x, y, inverseZ; };
    void triangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, std::uint32_t color);
    int width_, height_;
    std::vector<std::uint32_t> pixels_;
    std::vector<float> depth_;
};

// Values match Windows virtual keys; other backends can translate to these names.
enum class Key : std::uint16_t {
    None = 0, Cancel = 0x03, Backspace = 0x08, Tab = 0x09, Clear = 0x0c, Enter = 0x0d,
    Shift = 0x10, Control = 0x11, Alt = 0x12, Pause = 0x13, CapsLock = 0x14,
    Kana = 0x15, Hangul = Kana, ImeOn = 0x16, Junja = 0x17, Final = 0x18,
    Hanja = 0x19, Kanji = Hanja, ImeOff = 0x1a, Escape = 0x1b,
    Convert = 0x1c, NonConvert = 0x1d, Accept = 0x1e, ModeChange = 0x1f,
    Space = 0x20, PageUp = 0x21, PageDown = 0x22, End = 0x23, Home = 0x24,
    LeftArrow = 0x25, UpArrow = 0x26, RightArrow = 0x27, DownArrow = 0x28,
    Select = 0x29, Print = 0x2a, Execute = 0x2b, PrintScreen = 0x2c,
    Insert = 0x2d, Delete = 0x2e, Help = 0x2f,
    Digit0 = 0x30, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    A = 0x41, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftWindows = 0x5b, RightWindows = 0x5c, Menu = 0x5d, Sleep = 0x5f,
    Numpad0 = 0x60, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadMultiply = 0x6a, NumpadAdd, NumpadSeparator, NumpadSubtract, NumpadDecimal, NumpadDivide,
    F1 = 0x70, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
    NumLock = 0x90, ScrollLock = 0x91, NumpadEqual = 0x92,
    LeftShift = 0xa0, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
    BrowserBack = 0xa6, BrowserForward, BrowserRefresh, BrowserStop, BrowserSearch, BrowserFavorites, BrowserHome,
    VolumeMute = 0xad, VolumeDown, VolumeUp,
    MediaNextTrack = 0xb0, MediaPreviousTrack, MediaStop, MediaPlayPause,
    LaunchMail = 0xb4, LaunchMedia, LaunchApp1, LaunchApp2,
    Semicolon = 0xba, Equal = 0xbb, Comma = 0xbc, Minus = 0xbd, Period = 0xbe,
    Slash = 0xbf, Backquote = 0xc0, LeftBracket = 0xdb, Backslash = 0xdc,
    RightBracket = 0xdd, Quote = 0xde, Oem8 = 0xdf, InternationalBackslash = 0xe2,
    Process = 0xe5, Packet = 0xe7, Attention = 0xf6, CursorSelect = 0xf7,
    ExtendSelect = 0xf8, EraseEndOfFile = 0xf9, Play = 0xfa, Zoom = 0xfb, Pa1 = 0xfd, OemClear = 0xfe,
    NumpadEnter = 0x100, Count
};

enum class MouseButton { Left, Right, Middle, Back, Forward, Count };

struct ButtonState {
    bool down = false, pressed = false, released = false;
    void set(bool value) {
        if (value == down) return;
        down = value;
        if (value) pressed = true;
        else released = true;
    }
};

struct Viewport { int x = 0, y = 0, width = 0, height = 0; };

// The renderer and UI use the same letterboxed rectangle in client pixels.
inline Viewport fitViewport(int clientWidth, int clientHeight, int renderWidth, int renderHeight) {
    if (clientWidth <= 0 || clientHeight <= 0 || renderWidth <= 0 || renderHeight <= 0) return {};
    const int width = static_cast<int>(std::min<std::int64_t>(clientWidth,
        std::int64_t(clientHeight) * renderWidth / renderHeight));
    const int height = static_cast<int>(std::int64_t(width) * renderHeight / renderWidth);
    return {(clientWidth - width) / 2, (clientHeight - height) / 2, width, height};
}

struct Input {
    std::array<ButtonState, static_cast<std::size_t>(Key::Count)> keys{};
    std::array<ButtonState, static_cast<std::size_t>(MouseButton::Count)> mouseButtons{};
    bool focused = false;
    int mouseX = 0, mouseY = 0; // Client pixels, origin at the top-left.
    int renderWidth = 800, renderHeight = 500;
    Viewport viewport{0, 0, 800, 500};
    float mouseDeltaX = 0, mouseDeltaY = 0; // Raw movement accumulated this frame.
    float mouseWheel = 0, mouseWheelHorizontal = 0; // Wheel steps accumulated this frame.
    std::string text{}; // UTF-8 characters entered this frame, supplied by the platform.

    bool held(Key key) const { return state(keys, key).down; }
    bool pressed(Key key) const { return state(keys, key).pressed; }
    bool released(Key key) const { return state(keys, key).released; }
    bool held(MouseButton button) const { return state(mouseButtons, button).down; }
    bool pressed(MouseButton button) const { return state(mouseButtons, button).pressed; }
    bool released(MouseButton button) const { return state(mouseButtons, button).released; }
    void set(Key key, bool value) { const auto i = static_cast<std::size_t>(key); if (i < keys.size()) keys[i].set(value); }
    void set(MouseButton button, bool value) { const auto i = static_cast<std::size_t>(button); if (i < mouseButtons.size()) mouseButtons[i].set(value); }

    // Platform backends reset edges and deltas, then collect events for the new frame.
    void beginFrame() {
        for (auto& key : keys) key.pressed = key.released = false;
        for (auto& button : mouseButtons) button.pressed = button.released = false;
        mouseDeltaX = mouseDeltaY = mouseWheel = mouseWheelHorizontal = 0;
        text.clear();
    }
    void releaseAll() {
        for (auto& key : keys) { key.pressed = false; key.set(false); }
        for (auto& button : mouseButtons) { button.pressed = false; button.set(false); }
        mouseDeltaX = mouseDeltaY = mouseWheel = mouseWheelHorizontal = 0;
        focused = false;
        text.clear();
    }

private:
    template<class E, std::size_t N>
    static ButtonState state(const std::array<ButtonState, N>& states, E key) {
        const auto i = static_cast<std::size_t>(key);
        return i < N ? states[i] : ButtonState{};
    }
};

struct ScreenSettings {
    int width = 0, height = 0; // Zero keeps the initial/current rendering size.
    bool fullscreen = false; // Borderless fullscreen on the current monitor.
    bool operator==(const ScreenSettings& other) const {
        return width == other.width && height == other.height && fullscreen == other.fullscreen;
    }
    bool operator!=(const ScreenSettings& other) const { return !(*this == other); }
};

// Own your game state in a subclass. The platform supplies input and elapsed seconds.
class Game {
public:
    virtual ~Game() = default;
    virtual void update(float seconds, const Input& input) = 0;
    // Mutable access for script components; existing const accessors remain compatible.
    virtual Scene& scene() { return const_cast<Scene&>(std::as_const(*this).scene()); }
    virtual const Scene& scene() const = 0;
    virtual std::string title() const { return "Tiny3D"; }
    virtual bool captureMouse() const { return false; }
    virtual bool shouldQuit() const { return false; }
    virtual ScreenSettings screenSettings() const { return {}; }
    virtual bool executeScripts() const { return true; }
};

// Native Windows window. Other platforms can use Renderer directly.
// A nonzero frameLimit is useful for automated smoke tests.
int run(Game& game, int width = 800, int height = 500, unsigned frameLimit = 0,
        graphics::BackendFactory rendererFactory = {});

} // namespace tiny3d
