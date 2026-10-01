#include "tiny3d/engine.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace tiny3d {

Mesh cube() {
    return {
        {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
         {-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f}},
        {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 4, 7}, {0, 7, 3},
         {1, 2, 6}, {1, 6, 5}, {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2}}
    };
}

Mesh plane() {
    return {{{-.5f, 0, -.5f}, {.5f, 0, -.5f}, {.5f, 0, .5f}, {-.5f, 0, .5f}},
            {{0, 2, 1}, {0, 3, 2}}};
}

Renderer::Renderer(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("Renderer dimensions must be positive");
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels_.resize(count);
    depth_.resize(count);
}

namespace {
struct ClipPlane {
    Vector3 normal;
    float offset;
    float distance(Vector3 v) const { return dot(normal, v) + offset; }
};

// Clip before projecting, including triangles that cross the camera's near plane.
std::vector<Vector3> clip(const std::vector<Vector3>& polygon, const ClipPlane& plane) {
    std::vector<Vector3> result;
    if (polygon.empty()) return result;
    result.reserve(polygon.size() + 1);
    Vector3 previous = polygon.back();
    float previousDistance = plane.distance(previous);
    for (Vector3 current : polygon) {
        const float currentDistance = plane.distance(current);
        if ((currentDistance >= 0) != (previousDistance >= 0)) {
            const float t = previousDistance / (previousDistance - currentDistance);
            result.push_back(previous + (current - previous) * t);
        }
        if (currentDistance >= 0) result.push_back(current);
        previous = current;
        previousDistance = currentDistance;
    }
    return result;
}

Color shade(Color color, float brightness) {
    return {static_cast<std::uint8_t>(color.r * brightness),
            static_cast<std::uint8_t>(color.g * brightness),
            static_cast<std::uint8_t>(color.b * brightness)};
}
} // namespace

void Renderer::render(const Scene& scene, Color background) {
    std::fill(pixels_.begin(), pixels_.end(), background.packed());
    std::fill(depth_.begin(), depth_.end(), 0.0f);

    const Camera* activeCamera = nullptr;
    const Transform* cameraTransform = nullptr;
    for (const Entity& entity : scene.entities) {
        if (!entity.visible) continue;
        for (const auto& component : entity.components) {
            const auto* camera = dynamic_cast<const Camera*>(component.get());
            if (camera && camera->enabled && (!activeCamera || camera->depth > activeCamera->depth)) {
                activeCamera = camera;
                cameraTransform = &entity.transform;
            }
        }
    }
    if (!activeCamera) return;
    const Camera& camera = *activeCamera;
    const Vector3 viewPosition = cameraTransform->position();
    const Quaternion viewRotation = cameraTransform->rotation();
    if (!(camera.nearPlane > 0 && camera.farPlane > camera.nearPlane &&
          camera.fieldOfView > 0 && camera.fieldOfView < pi)) {
        throw std::invalid_argument("Invalid camera projection");
    }
    const float halfY = std::tan(camera.fieldOfView * .5f);
    const float halfX = halfY * static_cast<float>(width_) / static_cast<float>(height_);
    const std::array<ClipPlane, 6> planes{{
        {{0, 0, 1}, -camera.nearPlane}, {{0, 0, -1}, camera.farPlane},
        {{1, 0, halfX}, 0}, {{-1, 0, halfX}, 0},
        {{0, 1, halfY}, 0}, {{0, -1, halfY}, 0}
    }};
    const Vector3 light = normalized({-.5f, 1, -.4f});
    const auto project = [&](Vector3 v) -> ScreenPoint {
        return {(v.x / (v.z * halfX) + 1) * .5f * static_cast<float>(width_),
                (1 - v.y / (v.z * halfY)) * .5f * static_cast<float>(height_), 1 / v.z};
    };

    for (const Entity& entity : scene.entities) {
        if (!entity.visible) continue;
        for (const auto& component : entity.components) {
            const auto* visual = dynamic_cast<const MeshRenderer*>(component.get());
            if (!visual || !visual->enabled || !visual->mesh) continue;
            const Mesh& mesh = *visual->mesh;
            std::vector<Vector3> world;
            world.reserve(mesh.vertices.size());
            for (Vector3 v : mesh.vertices) world.push_back(entity.transform.point(v));
            for (const auto& indices : mesh.triangles) {
                for (auto index : indices) {
                    if (index >= world.size()) throw std::invalid_argument("Mesh index out of bounds");
                }
                const Vector3 a = world[indices[0]], b = world[indices[1]], c = world[indices[2]];
                const Vector3 normal = cross(b - a, c - a);
                if (dot(normal, a - viewPosition) >= 0) continue; // Back face or degenerate.
                const float brightness = .25f + .75f * std::max(0.0f, dot(normalized(normal), light));
                const auto color = shade(visual->color, std::min(brightness, 1.0f)).packed();
                std::vector<Vector3> polygon;
                for (Vector3 v : {a, b, c}) polygon.push_back(inverseRotate(v - viewPosition, viewRotation));
                for (const auto& plane : planes) polygon = clip(polygon, plane);
                for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                    triangle(project(polygon[0]), project(polygon[i]), project(polygon[i + 1]), color);
                }
            }
        }
    }
}

void Renderer::triangle(ScreenPoint a, ScreenPoint b, ScreenPoint c, std::uint32_t color) {
    const auto edge = [](ScreenPoint p, ScreenPoint q, float x, float y) {
        return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
    };
    const float area = edge(a, b, c.x, c.y);
    if (std::abs(area) < 1e-6f) return;
    const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int maxX = std::min(width_ - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int maxY = std::min(height_ - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + .5f, py = static_cast<float>(y) + .5f;
            const float wa = edge(b, c, px, py) / area;
            const float wb = edge(c, a, px, py) / area;
            const float wc = edge(a, b, px, py) / area;
            if (wa < 0 || wb < 0 || wc < 0) continue;
            const float inverseZ = wa * a.inverseZ + wb * b.inverseZ + wc * c.inverseZ;
            const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
            if (inverseZ > depth_[index]) {
                depth_[index] = inverseZ;
                pixels_[index] = color;
            }
        }
    }
}

void Renderer::savePPM(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open image: " + path);
    file << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    for (auto pixel : pixels_) {
        const char rgb[]{static_cast<char>((pixel >> 16) & 255),
                         static_cast<char>((pixel >> 8) & 255), static_cast<char>(pixel & 255)};
        file.write(rgb, 3);
    }
    file.close();
    if (!file) throw std::runtime_error("Cannot write image: " + path);
}

} // namespace tiny3d
