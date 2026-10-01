#pragma once

#include "tiny3d/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tiny3d {

struct Color {
    std::uint8_t r = 255, g = 255, b = 255;
    std::uint32_t packed() const {
        return (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
};

struct Mesh {
    std::vector<Vector3> vertices;
    // Counterclockwise winding when viewed from outside the surface.
    std::vector<std::array<std::size_t, 3>> triangles;
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

    explicit MeshRenderer(std::shared_ptr<const Mesh> mesh = {}, Color color = {})
        : mesh(std::move(mesh)), color(color) {}
};

struct Camera : Component {
    float depth = 0; // The highest enabled camera depth supplies the view.
    float fieldOfView = pi / 3; // Vertical field of view in radians.
    float nearPlane = 0.1f;
    float farPlane = 100;
};

struct Entity {
    Transform transform{};
    bool visible = true;
    std::vector<std::unique_ptr<Component>> components;

    Entity() = default;
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) noexcept = default;
    Entity& operator=(Entity&& entity) noexcept {
        if (this == &entity) return *this;
        transform = entity.transform;
        visible = entity.visible;
        components = std::move(entity.components);
        parent_ = entity.parent_;
        transform.parent_ = parent_ ? &parent_->transform : nullptr;
        return *this;
    }

    Entity* parent() const { return parent_; }
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
    T& addComponent(Args&&... args) {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *component;
        components.push_back(std::move(component));
        return result;
    }

    template<class T>
    T* getComponent() {
        for (auto& component : components) {
            if (auto* found = dynamic_cast<T*>(component.get())) return found;
        }
        return nullptr;
    }

    template<class T>
    const T* getComponent() const {
        for (const auto& component : components) {
            if (auto* found = dynamic_cast<const T*>(component.get())) return found;
        }
        return nullptr;
    }

private:
    Entity* parent_ = nullptr;
};

struct Scene {
    // Appending entities does not invalidate parent pointers or entity references.
    std::deque<Entity> entities;
};

class Renderer {
public:
    Renderer(int width, int height);
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

struct Input {
    std::array<ButtonState, static_cast<std::size_t>(Key::Count)> keys{};
    std::array<ButtonState, static_cast<std::size_t>(MouseButton::Count)> mouseButtons{};
    bool focused = false;
    int mouseX = 0, mouseY = 0; // Client pixels, origin at the top-left.
    float mouseDeltaX = 0, mouseDeltaY = 0; // Raw movement accumulated this frame.
    float mouseWheel = 0, mouseWheelHorizontal = 0; // Wheel steps accumulated this frame.

    bool held(Key key) const { return keys[static_cast<std::size_t>(key)].down; }
    bool pressed(Key key) const { return keys[static_cast<std::size_t>(key)].pressed; }
    bool released(Key key) const { return keys[static_cast<std::size_t>(key)].released; }
    bool held(MouseButton button) const { return mouseButtons[static_cast<std::size_t>(button)].down; }
    bool pressed(MouseButton button) const { return mouseButtons[static_cast<std::size_t>(button)].pressed; }
    bool released(MouseButton button) const { return mouseButtons[static_cast<std::size_t>(button)].released; }
    void set(Key key, bool value) { keys[static_cast<std::size_t>(key)].set(value); }
    void set(MouseButton button, bool value) { mouseButtons[static_cast<std::size_t>(button)].set(value); }

    // Platform backends reset edges and deltas, then collect events for the new frame.
    void beginFrame() {
        for (auto& key : keys) key.pressed = key.released = false;
        for (auto& button : mouseButtons) button.pressed = button.released = false;
        mouseDeltaX = mouseDeltaY = mouseWheel = mouseWheelHorizontal = 0;
    }
    void releaseAll() {
        for (auto& key : keys) { key.pressed = false; key.set(false); }
        for (auto& button : mouseButtons) { button.pressed = false; button.set(false); }
        mouseDeltaX = mouseDeltaY = mouseWheel = mouseWheelHorizontal = 0;
        focused = false;
    }
};

// Own your game state in a subclass. The platform supplies input and elapsed seconds.
class Game {
public:
    virtual ~Game() = default;
    virtual void update(float seconds, const Input& input) = 0;
    virtual const Scene& scene() const = 0;
    virtual std::string title() const { return "Tiny3D"; }
    virtual bool captureMouse() const { return false; }
    virtual bool shouldQuit() const { return false; }
};

// Native Windows window. Other platforms can use Renderer directly.
// A nonzero frameLimit is useful for automated smoke tests.
int run(Game& game, int width = 800, int height = 500, unsigned frameLimit = 0);

} // namespace tiny3d
