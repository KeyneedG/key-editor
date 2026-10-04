#include "tiny3d/scene.hpp"
#include "tiny3d/json.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#include "tiny3d/eryscript.hpp"
#include <fstream>
#include <limits>
#include <unordered_map>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tiny3d {
namespace {
using J = json::Value;
using A = J::Array;
using O = J::Object;
J vector(Vector3 v) { return A{v.x, v.y, v.z}; }
J color(Color c) { return A{int(c.r), int(c.g), int(c.b)}; }
float number(const J& j) {
    double n = j.number();
    if (std::abs(n) > std::numeric_limits<float>::max()) throw std::runtime_error("Scene number is too large");
    return static_cast<float>(n);
}
std::size_t index(const J& j, std::size_t maximum) {
    double n = j.number();
    if (n < 0 || n >= double(maximum) || std::floor(n) != n) throw std::runtime_error("Scene index out of range");
    return static_cast<std::size_t>(n);
}
Vector3 vector(const J& j) {
    if (j.array().size() != 3) throw std::runtime_error("Expected three vector values");
    return {number(j.at(0)), number(j.at(1)), number(j.at(2))};
}
Color color(const J& j) {
    if (j.array().size() != 3) throw std::runtime_error("Expected three color values");
    return {static_cast<std::uint8_t>(index(j.at(0), 256)), static_cast<std::uint8_t>(index(j.at(1), 256)),
            static_cast<std::uint8_t>(index(j.at(2), 256))};
}
}

std::string serializeScene(const Scene& scene) {
    std::unordered_map<const Entity*, std::size_t> entities;
    std::unordered_map<const Mesh*, std::size_t> meshes;
    A meshList, entityList;
    for (std::size_t i = 0; i < scene.entities.size(); ++i) entities[&scene.entities[i]] = i;
    const auto reference = [&](const Entity* e) -> J {
        if (!e) return {};
        const auto found = entities.find(e);
        if (found == entities.end()) throw std::runtime_error("Scene reference points outside the scene");
        return found->second;
    };
    for (const Entity& e : scene.entities) {
        A components;
        for (const auto& owned : e.components) {
            const Component* c = owned.get();
            O data{{"enabled", c->enabled}};
            if (const auto* v = dynamic_cast<const MeshRenderer*>(c)) {
                // UI::refresh owns its generated graphics; save their source components only.
                if (e.getComponent<Image>() || e.getComponent<SimpleText>() || e.getComponent<InputField>()) continue;
                data["type"] = "MeshRenderer"; data["color"] = color(v->color); data["unlit"] = v->unlit;
                if (!v->mesh) data["mesh"] = {};
                else {
                    if (!meshes.contains(v->mesh.get())) {
                        meshes[v->mesh.get()] = meshList.size(); A vertices, triangles;
                        for (auto p : v->mesh->vertices) vertices.push_back(vector(p));
                        for (auto t : v->mesh->triangles) triangles.push_back(A{t[0], t[1], t[2]});
                        meshList.push_back(O{{"vertices", vertices}, {"triangles", triangles}});
                    }
                    data["mesh"] = meshes.at(v->mesh.get());
                }
            } else if (const auto* v = dynamic_cast<const Camera*>(c)) {
                data["type"] = "Camera"; data["depth"] = v->depth; data["layers"] = double(v->layers);
                data["clearColor"] = v->clearColor; data["orthographic"] = v->orthographic;
                data["orthographicSize"] = v->orthographicSize; data["fieldOfView"] = v->fieldOfView;
                data["nearPlane"] = v->nearPlane; data["farPlane"] = v->farPlane;
            } else if (const auto* v = dynamic_cast<const BoxCollider*>(c)) {
                data["type"] = "BoxCollider"; data["center"] = vector(v->center); data["size"] = vector(v->size);
            } else if (const auto* v = dynamic_cast<const SphereCollider*>(c)) {
                data["type"] = "SphereCollider"; data["center"] = vector(v->center); data["radius"] = v->radius;
            } else if (const auto* v = dynamic_cast<const CapsuleCollider*>(c)) {
                data["type"] = "CapsuleCollider"; data["center"] = vector(v->center);
                data["radius"] = v->radius; data["height"] = v->height;
            } else if (const auto* v = dynamic_cast<const Rigidbody*>(c)) {
                data["type"] = "Rigidbody"; data["velocity"] = vector(v->velocity); data["mass"] = v->mass;
                data["useGravity"] = v->useGravity; data["isKinematic"] = v->isKinematic;
            } else if (const auto* v = dynamic_cast<const EryScript*>(c)) {
                data["type"] = "EryScript"; data["code"] = v->code;
                data["runOnAwake"] = v->runOnAwake; data["runInUpdate"] = v->runInUpdate;
            } else if (const auto* v = dynamic_cast<const Canvas*>(c)) {
                data["type"] = "Canvas"; data["camera"] = reference(v->camera);
            } else if (const auto* v = dynamic_cast<const Image*>(c)) {
                data["type"] = "Image"; data["color"] = color(v->color); data["width"] = v->width; data["height"] = v->height;
            } else if (const auto* v = dynamic_cast<const SimpleText*>(c)) {
                data["type"] = "SimpleText"; data["text"] = v->text; data["color"] = color(v->color);
                data["pixelSize"] = v->pixelSize; data["centered"] = v->centered;
            } else if (const auto* v = dynamic_cast<const Button*>(c)) {
                data["type"] = "Button"; data["interactable"] = v->interactable;
                data["normalColor"] = color(v->normalColor); data["hoverColor"] = color(v->hoverColor);
                data["pressedColor"] = color(v->pressedColor); data["disabledColor"] = color(v->disabledColor);
            } else if (const auto* v = dynamic_cast<const ScrollRect*>(c)) {
                data["type"] = "ScrollRect"; data["content"] = reference(v->content);
                data["contentHeight"] = v->contentHeight; data["offset"] = v->offset; data["wheelSpeed"] = v->wheelSpeed;
            } else if (const auto* v = dynamic_cast<const InputField*>(c)) {
                data["type"] = "InputField"; data["text"] = v->text; data["placeholder"] = v->placeholder;
                data["interactable"] = v->interactable; data["multiline"] = v->multiline;
                data["maxLength"] = v->maxLength; data["pixelSize"] = v->pixelSize;
            } else throw std::runtime_error("Scene contains an unsupported custom component; add its serializer before saving");
            components.push_back(data);
        }
        const auto& t = e.transform;
        Vector3 position = t.localPosition;
        if (e.parent()) if (const auto* scroll = e.parent()->getComponent<ScrollRect>(); scroll && scroll->content == &e)
            position = scroll->contentPosition();
        entityList.push_back(O{{"name", e.name}, {"tag", e.tag}, {"visible", e.visible}, {"layer", int(e.layer)},
            {"parent", reference(e.parent())}, {"position", vector(position)}, {"scale", vector(t.localScale)},
            {"rotation", A{t.localRotation.x, t.localRotation.y, t.localRotation.z, t.localRotation.w}}, {"components", components}});
    }
    return json::stringify(O{{"version", 1}, {"meshes", meshList}, {"entities", entityList}});
}

Scene deserializeScene(std::string_view source) {
    const auto root = json::parse(source);
    if (root.at("version").number() != 1) throw std::runtime_error("Unsupported scene version");
    std::vector<std::shared_ptr<Mesh>> meshes;
    for (const auto& m : root.at("meshes").array()) {
        auto mesh = std::make_shared<Mesh>();
        for (const auto& v : m.at("vertices").array()) mesh->vertices.push_back(vector(v));
        for (const auto& t : m.at("triangles").array()) {
            if (t.array().size() != 3) throw std::runtime_error("Expected three triangle indices");
            mesh->triangles.push_back({index(t.at(0), mesh->vertices.size()), index(t.at(1), mesh->vertices.size()),
                                       index(t.at(2), mesh->vertices.size())});
        }
        meshes.push_back(mesh);
    }
    Scene scene;
    const auto& list = root.at("entities").array();
    scene.entities.resize(list.size());
    const auto reference = [&](const J& j) -> Entity* { return j.null() ? nullptr : &scene.entities[index(j, list.size())]; };
    for (std::size_t i = 0; i < list.size(); ++i) {
        Entity& e = scene.entities[i]; const auto& data = list[i];
        e.name = data.at("name").string(); e.tag = data.at("tag").string();
        e.layer = static_cast<std::uint8_t>(index(data.at("layer"), 32)); e.visible = data.at("visible").boolean();
        e.transform.localPosition = vector(data.at("position")); e.transform.localScale = vector(data.at("scale"));
        const auto& r = data.at("rotation");
        if (r.array().size() != 4) throw std::runtime_error("Expected four quaternion values");
        e.transform.localRotation = normalized(Quaternion{number(r.at(0)), number(r.at(1)), number(r.at(2)), number(r.at(3))});
        for (const auto& c : data.at("components").array()) {
            const std::string type = c.at("type").string(); Component* added = nullptr;
            if (type == "MeshRenderer") {
                auto& v = e.addComponent<MeshRenderer>(); added = &v;
                if (!c.at("mesh").null()) v.mesh = meshes[index(c.at("mesh"), meshes.size())];
                v.color = color(c.at("color")); v.unlit = c.at("unlit").boolean();
            } else if (type == "Camera") {
                auto& v = e.addComponent<Camera>(); added = &v;
                v.depth = number(c.at("depth")); v.layers = static_cast<std::uint32_t>(index(c.at("layers"), std::size_t{1} << 32));
                v.clearColor = c.at("clearColor").boolean(); v.orthographic = c.at("orthographic").boolean();
                v.orthographicSize = number(c.at("orthographicSize")); v.fieldOfView = number(c.at("fieldOfView"));
                v.nearPlane = number(c.at("nearPlane")); v.farPlane = number(c.at("farPlane"));
                if (v.nearPlane <= 0 || v.farPlane <= v.nearPlane || v.orthographicSize <= 0 || v.fieldOfView <= 0 || v.fieldOfView >= pi)
                    throw std::runtime_error("Invalid scene camera projection");
            } else if (type == "BoxCollider") {
                auto& v = e.addComponent<BoxCollider>(); added = &v; v.center = vector(c.at("center")); v.size = vector(c.at("size"));
            } else if (type == "SphereCollider") {
                auto& v = e.addComponent<SphereCollider>(); added = &v; v.center = vector(c.at("center")); v.radius = number(c.at("radius"));
            } else if (type == "CapsuleCollider") {
                auto& v = e.addComponent<CapsuleCollider>(); added = &v; v.center = vector(c.at("center"));
                v.radius = number(c.at("radius")); v.height = number(c.at("height"));
            } else if (type == "Rigidbody") {
                auto& v = e.addComponent<Rigidbody>(); added = &v; v.velocity = vector(c.at("velocity")); v.mass = number(c.at("mass"));
                v.useGravity = c.at("useGravity").boolean(); v.isKinematic = c.at("isKinematic").boolean();
            } else if (type == "EryScript") {
                added = &e.addComponent<EryScript>(c.at("code").string(), c.at("runOnAwake").boolean(), c.at("runInUpdate").boolean());
            } else if (type == "Canvas") { auto& v = e.addComponent<Canvas>(); added = &v; v.camera = reference(c.at("camera"));
            } else if (type == "Image") {
                auto& v = e.addComponent<Image>(); added = &v; v.color = color(c.at("color"));
                v.width = number(c.at("width")); v.height = number(c.at("height"));
                if (v.width <= 0 || v.height <= 0) throw std::runtime_error("Invalid image size");
            } else if (type == "SimpleText") {
                auto& v = e.addComponent<SimpleText>(); added = &v; v.text = c.at("text").string(); v.color = color(c.at("color"));
                v.pixelSize = number(c.at("pixelSize")); v.centered = c.at("centered").boolean();
                if (v.pixelSize <= 0) throw std::runtime_error("Invalid text size");
            } else if (type == "Button") {
                auto& v = e.addComponent<Button>(); added = &v; v.interactable = c.at("interactable").boolean();
                v.normalColor = color(c.at("normalColor")); v.hoverColor = color(c.at("hoverColor"));
                v.pressedColor = color(c.at("pressedColor")); v.disabledColor = color(c.at("disabledColor"));
            } else if (type == "ScrollRect") {
                auto& v = e.addComponent<ScrollRect>(); added = &v; v.content = reference(c.at("content"));
                v.contentHeight = number(c.at("contentHeight")); v.offset = number(c.at("offset")); v.wheelSpeed = number(c.at("wheelSpeed"));
                if (v.contentHeight < 0 || v.offset < 0 || v.wheelSpeed < 0) throw std::runtime_error("Invalid scroll settings");
            } else if (type == "InputField") {
                auto& v = e.addComponent<InputField>(); added = &v; v.text = c.at("text").string(); v.placeholder = c.at("placeholder").string();
                v.interactable = c.at("interactable").boolean(); v.multiline = c.at("multiline").boolean();
                v.maxLength = index(c.at("maxLength"), 10000001); v.pixelSize = number(c.at("pixelSize"));
                if (v.pixelSize <= 0) throw std::runtime_error("Invalid input text size");
            } else throw std::runtime_error("Unknown scene component: " + type);
            added->enabled = c.at("enabled").boolean();
        }
    }
    for (std::size_t i = 0; i < list.size(); ++i) scene.entities[i].setParent(reference(list[i].at("parent")), false);
    for (auto& e : scene.entities) {
        if (auto* scroll = e.getComponent<ScrollRect>(); scroll && scroll->content && scroll->content->parent() != &e)
            throw std::runtime_error("ScrollRect content must be a direct child");
    }
    return scene;
}

void saveScene(const Scene& scene, const std::filesystem::path& path) {
    const auto source = serializeScene(scene);
    auto temporary = path; temporary += ".tmp";
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot save scene: " + path.string());
        out << source << '\n'; out.close();
        if (!out) throw std::runtime_error("Cannot write scene: " + path.string());
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace scene: " + path.string());
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) { std::error_code ignored; std::filesystem::remove(temporary, ignored); throw; }
}
Scene loadScene(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open scene: " + path.string());
    std::string source((std::istreambuf_iterator<char>(in)), {});
    if (in.bad()) throw std::runtime_error("Cannot read scene: " + path.string());
    return deserializeScene(source);
}
} // namespace tiny3d
