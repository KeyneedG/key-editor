#pragma once

#include "tiny3d/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tiny3d {

struct Color {
    std::uint8_t r = 255, g = 255, b = 255;
    std::uint32_t packed() const {
        return (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
};

struct Mesh {
    std::vector<Vec3> vertices;
    // Counterclockwise winding when viewed from outside the surface.
    std::vector<std::array<std::size_t, 3>> triangles;
};

Mesh cube();   // Unit cube, centered at the origin.
Mesh plane();  // Unit square on XZ, facing +Y.

struct Entity {
    std::shared_ptr<const Mesh> mesh;
    Transform transform{};
    Color color{};
    bool visible = true;
};

struct Scene {
    std::vector<Entity> entities;
};

struct Camera {
    Vec3 position{};
    Vec3 rotation{};
    float fieldOfView = pi / 3; // Vertical field of view in radians.
    float nearPlane = 0.1f;
    float farPlane = 100;
};

class Renderer {
public:
    Renderer(int width, int height);
    void render(const Scene& scene, const Camera& camera, Color background = {28, 36, 52});
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

enum class Key { Forward, Backward, Left, Right, LookLeft, LookRight, LookUp, LookDown, Reset, Count };

struct Input {
    std::array<bool, static_cast<std::size_t>(Key::Count)> down{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> previous{};
    bool held(Key key) const { return down[static_cast<std::size_t>(key)]; }
    bool pressed(Key key) const {
        const auto i = static_cast<std::size_t>(key);
        return down[i] && !previous[i];
    }
};

// Own your game state in a subclass. The platform supplies input and elapsed seconds.
class Game {
public:
    virtual ~Game() = default;
    virtual void update(float seconds, const Input& input) = 0;
    virtual const Scene& scene() const = 0;
    virtual const Camera& camera() const = 0;
    virtual std::string title() const { return "Tiny3D"; }
};

// Native Windows window. Other platforms can use Renderer directly.
// A nonzero frameLimit is useful for automated smoke tests.
int run(Game& game, int width = 800, int height = 500, unsigned frameLimit = 0);

} // namespace tiny3d
