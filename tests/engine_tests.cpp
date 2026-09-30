#include "tiny3d/engine.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>

using namespace tiny3d;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void close(Vec3 actual, Vec3 expected, const char* message) {
    check(length(actual - expected) < 1e-4f, message);
}
template<class F> void rejects(F operation, const char* message) {
    try { operation(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
Entity surface(std::vector<Vec3> vertices, Color color) {
    return {std::make_shared<Mesh>(Mesh{std::move(vertices), {{0, 1, 2}}}), {}, color};
}
std::size_t painted(const Renderer& renderer) {
    return static_cast<std::size_t>(std::count_if(renderer.pixels().begin(), renderer.pixels().end(),
        [](auto color) { return color != 0; }));
}

void math() {
    close(cross({1, 0, 0}, {0, 1, 0}), {0, 0, 1}, "Cross product");
    close(normalized({0, 0, 0}), {}, "Normalize zero");
    close(normalized({3, 0, 4}), {.6f, 0, .8f}, "Normalize vector");
    const Vec3 value{2, -3, 4}, rotation{.4f, -.7f, 1.1f};
    close(inverseRotate(rotate(value, rotation), rotation), value, "Inverse rotation");
    Transform transform{{1, 2, 3}, {0, pi / 2, 0}, {2, 3, 4}};
    close(transform.point({1, 0, 0}), {1, 2, 1}, "Scale, rotate, translate");
}

void visibilityAndCamera() {
    Renderer renderer(64, 64);
    Camera camera;
    Scene scene{{surface({{-.8f, -.8f, 2}, {0, .8f, 2}, {.8f, -.8f, 2}}, {255, 0, 0})}};
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) > 100, "Triangle in front of camera");
    scene.entities[0].visible = false;
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) == 0, "Hidden entities and buffer clearing");
    scene.entities[0].visible = true;
    auto reversed = std::make_shared<Mesh>(*scene.entities[0].mesh);
    std::swap(reversed->triangles[0][0], reversed->triangles[0][1]);
    scene.entities[0].mesh = reversed;
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) == 0, "Back-face culling");

    scene.entities[0] = {std::make_shared<Mesh>(cube())};
    scene.entities[0].transform.position.z = -3;
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) == 0, "Geometry behind camera");
    camera.rotation.y = pi;
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) > 100, "Rotated camera");
    camera.position.x = 10;
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) == 0, "Translated camera");
}

void clipping() {
    Renderer renderer(64, 64);
    Camera camera;
    camera.nearPlane = .2f;
    camera.farPlane = 5;
    Scene scene{{surface({{-.6f, -.6f, 1}, {0, .6f, .05f}, {.6f, -.6f, 1}}, {255, 255, 255})}};
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) > 100, "Triangle crossing near plane must survive");
    scene.entities[0] = surface({{-.6f, -.6f, 4}, {0, .6f, 8}, {.6f, -.6f, 4}}, {255, 255, 255});
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) > 0, "Triangle crossing far plane must survive");
    scene.entities[0].transform.position.z = 6;
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) == 0, "Geometry beyond far plane");

    auto quad = std::make_shared<Mesh>(Mesh{
        {{-10, -10, 2}, {10, -10, 2}, {10, 10, 2}, {-10, 10, 2}}, {{0, 2, 1}, {0, 3, 2}}});
    scene.entities[0] = {quad};
    renderer.render(scene, camera, {0, 0, 0});
    check(painted(renderer) == renderer.pixels().size(), "Side-plane clipping and shared triangle edges");
}

void depth() {
    Renderer renderer(64, 64);
    Camera camera;
    camera.fieldOfView = pi / 2;
    // Both triangles have the same projected outline. The red one slopes in depth.
    Scene scene{{surface({{-2, -2, 2}, {0, 6, 6}, {2, -2, 2}}, {255, 0, 0}),
                 surface({{-3, -3, 3}, {0, 3, 3}, {3, -3, 3}}, {0, 255, 0})}};
    renderer.render(scene, camera, {0, 0, 0});
    const auto first = renderer.pixels();
    check((first[8 * 64 + 32] & 0xff00) != 0, "Flat green surface nearer at top");
    check((first[56 * 64 + 32] & 0xff0000) != 0, "Sloping red surface nearer at bottom");
    std::reverse(scene.entities.begin(), scene.entities.end());
    renderer.render(scene, camera, {0, 0, 0});
    check(renderer.pixels() == first, "Depth must be perspective-correct and independent of draw order");
}

void validationAndOutput() {
    rejects([] { Renderer renderer(0, 10); }, "Reject zero-sized image");
    Renderer renderer(2, 2);
    Camera camera;
    camera.nearPlane = 0;
    rejects([&] { renderer.render({}, camera); }, "Reject invalid camera");
    camera = {};
    Scene invalid{{surface({{0, 0, 1}}, {})}};
    rejects([&] { renderer.render(invalid, camera); }, "Reject invalid mesh indices");

    renderer.render({}, camera, {10, 20, 30});
    const std::string path = "engine-tests.ppm";
    renderer.savePPM(path);
    std::ifstream file(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close();
    std::remove(path.c_str());
    const std::string header = "P6\n2 2\n255\n";
    check(bytes.size() == header.size() + 12 && bytes.substr(0, header.size()) == header, "PPM header and byte count");
    check(bytes.substr(header.size(), 3) == std::string("\x0a\x14\x1e", 3), "PPM channel order");

    Input input;
    input.down[static_cast<std::size_t>(Key::Reset)] = true;
    check(input.pressed(Key::Reset), "New key press");
    input.previous = input.down;
    check(input.held(Key::Reset) && !input.pressed(Key::Reset), "Held key is not repeatedly pressed");
}
} // namespace

int main() {
    try {
        math();
        visibilityAndCamera();
        clipping();
        depth();
        validationAndOutput();
        std::cout << "All engine tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
