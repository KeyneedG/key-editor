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
    case '|': return {4,4,4,4,4,4,4};
    case '*': return {0,21,14,31,14,21,0};
    case '\\': return {16,8,8,4,2,2,1};
    case '"': return {10,10,10,0,0,0,0};
    case '\'': return {4,4,4,0,0,0,0};
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

bool clippedHit(const Entity& entity, const Image& image, const Canvas& canvas, const Input& input, float& distance) {
    if (!hit(entity, image, canvas, input, distance)) return false;
    for (const Entity* p = entity.parent(); p; p = p->parent()) {
        const auto* scroll = p->getComponent<ScrollRect>();
        if (!scroll || !scroll->enabled) continue;
        const auto* rect = p->getComponent<Image>(); float ignored = 0;
        if (!rect || !hit(*p, *rect, canvas, input, ignored)) return false;
    }
    return true;
}

// Clip generated triangles in each ancestor's local rectangle, then return to local space.
std::shared_ptr<const Mesh> clippedMesh(const Entity& entity, std::shared_ptr<const Mesh> source,
                                     detail::UIClipCache& cache, const Entity* ownRect = nullptr) {
    std::vector<const Entity*> masks;
    if (ownRect) masks.push_back(ownRect);
    for (const Entity* p = entity.parent(); p; p = p->parent())
        if (const auto* s = p->getComponent<ScrollRect>(); s && s->enabled && p->getComponent<Image>()) masks.push_back(p);
    if (masks.empty() || !source) { cache = {}; return source; }
    std::vector<float> state;
    const auto matrix = [&](const Transform& transform) {
        for (auto v : {transform.position(), transform.vector({1, 0, 0}), transform.vector({0, 1, 0}), transform.vector({0, 0, 1})})
            state.insert(state.end(), {v.x, v.y, v.z});
    };
    matrix(entity.transform);
    for (const Entity* mask : masks) { matrix(mask->transform); const auto* image = mask->getComponent<Image>(); state.insert(state.end(), {image->width, image->height}); }
    if (cache.source == source && cache.state == state) return cache.result;
    for (const Entity* p = &entity; p; p = p->parent()) {
        auto s = p->transform.localScale;
        if (s.x == 0 || s.y == 0 || s.z == 0) return std::make_shared<Mesh>();
    }
    auto result = std::make_shared<Mesh>();
    for (auto triangle : source->triangles) {
        std::vector<Vector3> polygon;
        for (auto i : triangle) polygon.push_back(entity.transform.point(source->vertices[i]));
        for (const Entity* mask : masks) {
            const auto* image = mask->getComponent<Image>();
            for (auto& v : polygon) v = mask->transform.inversePoint(v);
            for (int side = 0; side < 4 && !polygon.empty(); ++side) {
                const auto distance = [&](Vector3 v) { return side == 0 ? v.x + image->width * .5f :
                    side == 1 ? image->width * .5f - v.x : side == 2 ? v.y + image->height * .5f : image->height * .5f - v.y; };
                std::vector<Vector3> next;
                Vector3 previous = polygon.back(); float d0 = distance(previous);
                for (auto v : polygon) {
                    float d1 = distance(v);
                    if ((d0 >= 0) != (d1 >= 0)) next.push_back(previous + (v - previous) * (d0 / (d0 - d1)));
                    if (d1 >= 0) next.push_back(v);
                    previous = v; d0 = d1;
                }
                polygon = std::move(next);
            }
            for (auto& v : polygon) v = mask->transform.point(v);
        }
        const auto first = result->vertices.size();
        for (auto v : polygon) result->vertices.push_back(entity.transform.inversePoint(v));
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) result->triangles.push_back({first, first + i, first + i + 1});
    }
    cache.source = source; cache.state = std::move(state); cache.result = result;
    return cache.result;
}

std::size_t previousCharacter(const std::string& s, std::size_t i) {
    if (i) --i;
    while (i && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) --i;
    return i;
}
std::size_t nextCharacter(const std::string& s, std::size_t i) {
    if (i < s.size()) ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) ++i;
    return i;
}

} // namespace

void UI::refresh(Scene& scene) {
    for (Entity& entity : scene.entities) {
        auto* scroll = entity.getComponent<ScrollRect>(); const auto* rect = entity.getComponent<Image>();
        if (!scroll || !scroll->enabled || !rect || !scroll->content) continue;
        if (!std::isfinite(scroll->contentHeight) || !std::isfinite(scroll->offset) || !std::isfinite(scroll->wheelSpeed))
            throw std::invalid_argument("Scroll settings must be finite");
        if (scroll->content->parent() != &entity) throw std::invalid_argument("ScrollRect content must be a direct child");
        if (scroll->trackedContent_ != scroll->content) {
            scroll->trackedContent_ = scroll->content; scroll->origin_ = scroll->content->transform.localPosition;
        }
        scroll->offset = std::clamp(scroll->offset, 0.f, std::max(0.f, scroll->contentHeight - rect->height));
        scroll->content->transform.localPosition = scroll->origin_ + Vector3{0, scroll->offset, 0};
    }
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
            if (!image->sourceMesh_ || image->width != image->builtWidth_ || image->height != image->builtHeight_) {
                auto mesh = std::make_shared<Mesh>();
                quad(*mesh, -image->width * .5f, -image->height * .5f, image->width, image->height);
                image->sourceMesh_ = mesh;
                image->builtWidth_ = image->width; image->builtHeight_ = image->height;
            }
            visual->mesh = clippedMesh(entity, image->sourceMesh_, image->clip_);
            visual->color = image->color;
            if (const auto* button = entity.getComponent<Button>(); button && button->enabled) {
                visual->color = !button->interactable ? button->disabledColor : button->pressed ? button->pressedColor :
                    button->hovered ? button->hoverColor : button->normalColor;
            }
        } else {
            if (!std::isfinite(text->pixelSize) || text->pixelSize <= 0) {
                throw std::invalid_argument("UI text pixel size must be positive and finite");
            }
            if (!text->sourceMesh_ || text->text != text->builtText_ || text->pixelSize != text->builtPixelSize_ ||
                text->centered != text->builtCentered_) {
                text->sourceMesh_ = textMesh(*text);
                text->builtText_ = text->text; text->builtPixelSize_ = text->pixelSize; text->builtCentered_ = text->centered;
            }
            visual->mesh = clippedMesh(entity, text->sourceMesh_, text->clip_);
            visual->color = text->color;
        }
        if (auto* field = entity.getComponent<InputField>(); field && image) {
            if (!std::isfinite(field->pixelSize) || field->pixelSize <= 0) throw std::invalid_argument("Input text size must be positive and finite");
            if (!field->textVisual_) field->textVisual_ = &entity.addComponent<MeshRenderer>();
            auto& ink = *field->textVisual_;
            ink.enabled = field->enabled && visual->enabled; ink.unlit = true;
            ink.color = field->text.empty() && !field->focused ? Color{140, 150, 166} : Color{240, 244, 255};
            std::string display = field->text.empty() && !field->focused ? field->placeholder : field->text;
            if (field->focused) display.insert(std::min(cursor_, display.size()), "|");
            // Keep the caret visible in a single-line field.
            if (!field->multiline) {
                const auto columns = static_cast<std::size_t>(std::max(1.f, (image->width - 12) / (6 * field->pixelSize)));
                if (field->focused && cursor_ >= columns) display.erase(0, cursor_ - columns + 1);
            }
            if (!field->sourceMesh_ || display != field->builtText_ || field->pixelSize != field->builtPixelSize_ ||
                image->width != field->builtWidth_ || image->height != field->builtHeight_) {
                SimpleText label; label.text = display; label.pixelSize = field->pixelSize; label.centered = false;
                auto mesh = textMesh(label);
                for (auto& v : mesh->vertices) { v.x += -image->width * .5f + 6; v.y += image->height * .5f - 6; v.z = -.02f; }
                field->sourceMesh_ = mesh; field->builtText_ = display; field->builtPixelSize_ = field->pixelSize;
                field->builtWidth_ = image->width; field->builtHeight_ = image->height;
            }
            ink.mesh = clippedMesh(entity, field->sourceMesh_, field->clip_, &entity);
        }
    }
}

void UI::cancel() {
    pressed_ = nullptr;
    if (focused_) if (auto* field = focused_->getComponent<InputField>()) field->focused = false;
    focused_ = nullptr; initialText_.clear(); cursor_ = 0; selectAll_ = false;
}

void UI::focus(Entity& entity) {
    if (focused_) focused_->getComponent<InputField>()->focused = false;
    auto* field = entity.getComponent<InputField>();
    focused_ = field && field->enabled && field->interactable ? &entity : nullptr;
    if (focused_) { field->focused = true; initialText_ = field->text; cursor_ = field->text.size(); selectAll_ = true; }
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
            const auto* field = entity.getComponent<InputField>();
            if (!entity.visibleInHierarchy() ||
                !((button && button->enabled && button->interactable) || (field && field->enabled && field->interactable)) ||
                !image || !image->enabled || !canvas) continue;
            float distance = 0;
            if (!clippedHit(entity, *image, *canvas, input, distance)) continue;
            const float depth = canvas->camera->getComponent<Camera>()->depth;
            if (!hovered || depth > cameraDepth || (depth == cameraDepth && distance < closest)) {
                hovered = &entity; cameraDepth = depth; closest = distance;
            }
        }
    }
    std::vector<std::function<void()>> callbacks;
    const auto commit = [&] {
        if (!focused_) return;
        auto* field = focused_->getComponent<InputField>();
        field->focused = false;
        if (field->onSubmit) callbacks.push_back([action = field->onSubmit, value = field->text] { action(value); });
        focused_ = nullptr; selectAll_ = false;
    };
    if (focused_ && (!input.focused || !focused_->visibleInHierarchy() || !focused_->getComponent<InputField>()->enabled)) commit();
    if (input.pressed(MouseButton::Left) && hovered != focused_) {
        commit();
        if (hovered) if (auto* field = hovered->getComponent<InputField>()) {
            (void)field; focus(*hovered);
        }
    }
    if (focused_) {
        auto& field = *focused_->getComponent<InputField>();
        cursor_ = std::min(cursor_, field.text.size());
        if (input.held(Key::Control) && input.pressed(Key::A)) selectAll_ = true;
        const auto eraseSelection = [&] { if (selectAll_) { field.text.clear(); cursor_ = 0; selectAll_ = false; return true; } return false; };
        if (input.pressed(Key::Backspace) && !eraseSelection() && cursor_) {
            auto p = previousCharacter(field.text, cursor_); field.text.erase(p, cursor_ - p); cursor_ = p;
        }
        if (input.pressed(Key::Delete) && !eraseSelection() && cursor_ < field.text.size()) field.text.erase(cursor_, nextCharacter(field.text, cursor_) - cursor_);
        if (input.pressed(Key::LeftArrow)) { cursor_ = previousCharacter(field.text, cursor_); selectAll_ = false; }
        if (input.pressed(Key::RightArrow)) { cursor_ = nextCharacter(field.text, cursor_); selectAll_ = false; }
        if (input.pressed(Key::Home)) { cursor_ = 0; selectAll_ = false; }
        if (input.pressed(Key::End)) { cursor_ = field.text.size(); selectAll_ = false; }
        std::string entered = input.text;
        if (field.multiline && input.pressed(Key::Enter) && !input.held(Key::Control)) entered += '\n';
        if (!entered.empty() && !input.held(Key::Control)) {
            eraseSelection();
            if (field.text.size() + entered.size() <= field.maxLength) { field.text.insert(cursor_, entered); cursor_ += entered.size(); }
        }
        if (input.pressed(Key::Escape)) { field.text = initialText_; field.focused = false; focused_ = nullptr; selectAll_ = false; }
        else if ((input.pressed(Key::Enter) || input.pressed(Key::NumpadEnter)) && (!field.multiline || input.held(Key::Control))) commit();
    }
    Entity* scrolled = nullptr; float scrollDepth = -std::numeric_limits<float>::infinity(), scrollDistance = std::numeric_limits<float>::infinity();
    if (input.focused && input.mouseWheel != 0) for (auto& entity : scene.entities) {
        auto* scroll = entity.getComponent<ScrollRect>(); const auto* image = entity.getComponent<Image>(); const auto* canvas = canvasOf(entity);
        if (!scroll || !scroll->enabled || !image || !canvas || !entity.visibleInHierarchy()) continue;
        float distance = 0;
        if (!clippedHit(entity, *image, *canvas, input, distance)) continue;
        float depth = canvas->camera->getComponent<Camera>()->depth;
        if (depth > scrollDepth || (depth == scrollDepth && distance < scrollDistance)) { scrolled = &entity; scrollDepth = depth; scrollDistance = distance; }
    }
    if (scrolled) { auto& s = *scrolled->getComponent<ScrollRect>(); s.offset -= input.mouseWheel * s.wheelSpeed; }
    if (input.pressed(MouseButton::Left)) pressed_ = hovered;
    if (input.released(MouseButton::Left)) {
        if (hovered && hovered == pressed_) if (auto* button = hovered->getComponent<Button>(); button && button->onClick) callbacks.push_back(button->onClick);
        pressed_ = nullptr;
    }
    if (!input.held(MouseButton::Left) || !input.focused) pressed_ = nullptr;
    for (Entity& entity : scene.entities) {
        if (auto* button = entity.getComponent<Button>()) {
            button->hovered = &entity == hovered;
            button->pressed = &entity == pressed_ && &entity == hovered;
        }
    }
    for (auto& action : callbacks) action(); // Copy callbacks before any of them can rebuild the scene.
    refresh(scene);
}

} // namespace tiny3d
