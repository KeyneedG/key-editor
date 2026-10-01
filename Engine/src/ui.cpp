#include "tiny3d/ui.hpp"

#include <array>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace tiny3d {
namespace {

void quad(Mesh& mesh, float x, float y, float width, float height) {
    const auto first = mesh.vertices.size();
    mesh.vertices.insert(mesh.vertices.end(), {{x, y, 0}, {x + width, y, 0},
        {x + width, y + height, 0}, {x, y + height, 0}});
    mesh.triangles.push_back({first, first + 2, first + 1}); // Faces -Z, toward the UI camera.
    mesh.triangles.push_back({first, first + 3, first + 2});
}

// Built-in 5x7 ink patterns. Lowercase uses the same shapes as uppercase.
std::array<unsigned, 7> glyph(unsigned char character) {
    static constexpr unsigned letters[26][7]{
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    static constexpr unsigned digits[10][7]{
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    character = static_cast<unsigned char>(std::toupper(character));
    const unsigned* rows = nullptr;
    if (character >= 'A' && character <= 'Z') rows = letters[character - 'A'];
    if (character >= '0' && character <= '9') rows = digits[character - '0'];
    if (rows) { std::array<unsigned, 7> result{}; std::copy(rows, rows + 7, result.begin()); return result; }
    switch (character) {
    case ' ': return {};
    case '.': return {0,0,0,0,0,6,6};
    case ',': return {0,0,0,0,6,6,4};
    case ':': return {0,6,6,0,6,6,0};
    case ';': return {0,6,6,0,6,6,4};
    case '-': return {0,0,0,31,0,0,0};
    case '_': return {0,0,0,0,0,0,31};
    case '+': return {0,4,4,31,4,4,0};
    case '=': return {0,0,31,0,31,0,0};
    case '/': return {1,2,2,4,8,8,16};
    case '!': return {4,4,4,4,4,0,4};
    case '?': return {14,17,1,2,4,0,4};
    case '(': return {2,4,8,8,8,4,2};
    case ')': return {8,4,2,2,2,4,8};
    case '[': return {14,8,8,8,8,8,14};
    case ']': return {14,2,2,2,2,2,14};
    case '<': return {1,2,4,8,4,2,1};
    case '>': return {16,8,4,2,4,8,16};
    case '%': return {17,2,4,4,8,16,17};
    default: return {14,17,1,2,4,0,4};
    }
}

std::shared_ptr<Mesh> textMesh(const SimpleText& text) {
    std::size_t columns = 0, longest = 0, lines = 1;
    for (char character : text.text) {
        if (character == '\n') { longest = std::max(longest, columns); columns = 0; ++lines; }
        else ++columns;
    }
    longest = std::max(longest, columns);
    const float width = longest ? (static_cast<float>(longest) * 6 - 1) * text.pixelSize : 0;
    const float height = (static_cast<float>(lines) * 8 - 1) * text.pixelSize;
    const float left = text.centered ? -width * .5f : 0, top = text.centered ? height * .5f : 0;
    auto mesh = std::make_shared<Mesh>();
    float x = left, y = top;
    for (unsigned char character : text.text) {
        if (character == '\n') { x = left; y -= 8 * text.pixelSize; continue; }
        const auto rows = glyph(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if (rows[row] & (1u << (4 - column))) {
                    quad(*mesh, x + column * text.pixelSize, y - (row + 1) * text.pixelSize,
                        text.pixelSize, text.pixelSize);
                }
            }
        }
        x += 6 * text.pixelSize;
    }
    return mesh;
}

const Canvas* canvasOf(const Entity& entity) {
    for (const Entity* parent = &entity; parent; parent = parent->parent()) {
        if (const auto* canvas = parent->getComponent<Canvas>()) return canvas;
    }
    return nullptr;
}

bool hit(const Entity& entity, const Image& image, const Canvas& canvas, const Input& input, float& distance) {
    if (!canvas.enabled || !canvas.camera || !canvas.camera->visibleInHierarchy() ||
        input.viewport.width <= 0 || input.viewport.height <= 0 || input.renderWidth <= 0 || input.renderHeight <= 0 ||
        input.mouseX < input.viewport.x || input.mouseY < input.viewport.y ||
        input.mouseX >= input.viewport.x + input.viewport.width || input.mouseY >= input.viewport.y + input.viewport.height) return false;
    const auto* camera = canvas.camera->getComponent<Camera>();
    if (!camera || !camera->enabled || !(camera->layers & layerMask(entity.layer))) return false;
    const float x = 2 * (input.mouseX + .5f - input.viewport.x) / input.viewport.width - 1;
    const float y = 1 - 2 * (input.mouseY + .5f - input.viewport.y) / input.viewport.height;
    const float halfY = camera->orthographic ? camera->orthographicSize : std::tan(camera->fieldOfView * .5f);
    const float halfX = halfY * input.renderWidth / input.renderHeight;
    const Transform& view = canvas.camera->transform;
    const Vector3 origin = view.position() + (camera->orthographic ? rotate({x * halfX, y * halfY, 0}, view.rotation()) : Vector3{});
    const Vector3 direction = rotate(camera->orthographic ? Vector3{0, 0, 1} : normalized(Vector3{x * halfX, y * halfY, 1}), view.rotation());
    const Vector3 normal = cross(entity.transform.vector({0, 1, 0}), entity.transform.vector({1, 0, 0}));
    if (dot(normal, direction) >= 0) return false; // Match the renderer's front faces.
    for (const Entity* parent = &entity; parent; parent = parent->parent()) {
        const Vector3 scale = parent->transform.localScale;
        if (scale.x == 0 || scale.y == 0 || scale.z == 0) return false;
    }
    const Vector3 localOrigin = entity.transform.inversePoint(origin);
    const Vector3 localDirection = entity.transform.inversePoint(origin + direction) - localOrigin;
    if (std::abs(localDirection.z) < 1e-6f) return false;
    distance = -localOrigin.z / localDirection.z;
    if (distance <= 0) return false;
    const Vector3 point = localOrigin + localDirection * distance;
    const float depth = inverseRotate(origin + direction * distance - view.position(), view.rotation()).z;
    return depth >= camera->nearPlane && depth <= camera->farPlane &&
        std::abs(point.x) <= image.width * .5f && std::abs(point.y) <= image.height * .5f;
}

} // namespace

void UI::refresh(Scene& scene) {
    for (Entity& entity : scene.entities) {
        auto* image = entity.getComponent<Image>();
        auto* text = entity.getComponent<SimpleText>();
        if (!image && !text) continue;
        auto* visual = entity.getComponent<MeshRenderer>();
        if (!visual) visual = &entity.addComponent<MeshRenderer>();
        const Canvas* canvas = canvasOf(entity);
        visual->enabled = canvas && canvas->enabled && (image ? image->enabled : text->enabled);
        visual->unlit = true;
        if (!visual->enabled) continue;
        if (image) {
            if (!std::isfinite(image->width) || !std::isfinite(image->height) || image->width <= 0 || image->height <= 0) {
                throw std::invalid_argument("UI image dimensions must be positive and finite");
            }
            if (!visual->mesh || image->width != image->builtWidth_ || image->height != image->builtHeight_) {
                auto mesh = std::make_shared<Mesh>();
                quad(*mesh, -image->width * .5f, -image->height * .5f, image->width, image->height);
                visual->mesh = mesh;
                image->builtWidth_ = image->width; image->builtHeight_ = image->height;
            }
            visual->color = image->color;
            if (const auto* button = entity.getComponent<Button>(); button && button->enabled) {
                visual->color = !button->interactable ? button->disabledColor : button->pressed ? button->pressedColor :
                    button->hovered ? button->hoverColor : button->normalColor;
            }
        } else {
            if (!std::isfinite(text->pixelSize) || text->pixelSize <= 0) {
                throw std::invalid_argument("UI text pixel size must be positive and finite");
            }
            if (!visual->mesh || text->text != text->builtText_ || text->pixelSize != text->builtPixelSize_ ||
                text->centered != text->builtCentered_) {
                visual->mesh = textMesh(*text);
                text->builtText_ = text->text; text->builtPixelSize_ = text->pixelSize; text->builtCentered_ = text->centered;
            }
            visual->color = text->color;
        }
    }
}

void UI::update(Scene& scene, const Input& input) {
    refresh(scene);
    Entity* hovered = nullptr;
    float closest = std::numeric_limits<float>::infinity(), cameraDepth = -std::numeric_limits<float>::infinity();
    if (input.focused) {
        for (Entity& entity : scene.entities) {
            const auto* button = entity.getComponent<Button>();
            const auto* image = entity.getComponent<Image>();
            const auto* canvas = canvasOf(entity);
            if (!entity.visibleInHierarchy() || !button || !button->enabled || !button->interactable ||
                !image || !image->enabled || !canvas) continue;
            float distance = 0;
            if (!hit(entity, *image, *canvas, input, distance)) continue;
            const float depth = canvas->camera->getComponent<Camera>()->depth;
            if (!hovered || depth > cameraDepth || (depth == cameraDepth && distance < closest)) {
                hovered = &entity; cameraDepth = depth; closest = distance;
            }
        }
    } else cancel();
    if (input.pressed(MouseButton::Left)) pressed_ = hovered;
    std::function<void()> clicked;
    if (input.released(MouseButton::Left)) {
        if (hovered && hovered == pressed_) clicked = hovered->getComponent<Button>()->onClick;
        cancel();
    }
    if (!input.held(MouseButton::Left)) cancel();
    for (Entity& entity : scene.entities) {
        if (auto* button = entity.getComponent<Button>()) {
            button->hovered = &entity == hovered;
            button->pressed = &entity == pressed_ && &entity == hovered;
        }
    }
    if (clicked) clicked(); // Copy the callback: it may rebuild the scene or hide this menu.
    refresh(scene);
}

} // namespace tiny3d
