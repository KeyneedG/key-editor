#include "tiny3d/gizmos.hpp"
#include "tiny3d/behaviour.hpp"
#include "tiny3d/physics.hpp"

namespace tiny3d {
namespace {
thread_local std::vector<GizmoLine>* drawing = nullptr;
bool finite(Vector3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
Vector3 absolute(Vector3 p) { return {std::abs(p.x), std::abs(p.y), std::abs(p.z)}; }
void circle(Vector3 center, Vector3 u, Vector3 v, float radius) {
    for (int i = 0; i < 32; ++i) {
        float a = 2 * pi * i / 32, b = 2 * pi * (i + 1) / 32;
        Gizmos::line(center + (u * std::cos(a) + v * std::sin(a)) * radius,
                     center + (u * std::cos(b) + v * std::sin(b)) * radius);
    }
}
}
void Gizmos::line(Vector3 from, Vector3 to) {
    if (!drawing) return;
    if (!finite(from) || !finite(to)) throw std::invalid_argument("Gizmo coordinates must be finite");
    if (drawing->size() >= 100000) throw std::runtime_error("Too many gizmo lines in one frame");
    drawing->push_back({from, to, color});
}
void Gizmos::wireCube(Vector3 center, Vector3 size, Quaternion rotation) {
    if (!drawing) return;
    std::array<Vector3, 8> corners;
    for (int i = 0; i < 8; ++i) corners[i] = center + rotate({(i & 1 ? .5f : -.5f) * size.x,
        (i & 2 ? .5f : -.5f) * size.y, (i & 4 ? .5f : -.5f) * size.z}, rotation);
    for (int i = 0; i < 8; ++i) for (int axis : {1, 2, 4}) if (!(i & axis)) line(corners[i], corners[i | axis]);
}
void Gizmos::wireSphere(Vector3 center, float radius) {
    if (!drawing) return;
    if (!std::isfinite(radius) || radius <= 0) throw std::invalid_argument("Gizmo radius must be positive and finite");
    circle(center, {1,0,0}, {0,1,0}, radius); circle(center, {1,0,0}, {0,0,1}, radius); circle(center, {0,1,0}, {0,0,1}, radius);
}
void Gizmos::wireCapsule(Vector3 center, float radius, float height, Quaternion rotation) {
    if (!drawing) return;
    if (!std::isfinite(radius) || !std::isfinite(height) || radius <= 0 || height < 2 * radius)
        throw std::invalid_argument("Gizmo capsule height must include its positive-radius caps");
    const auto up = rotate({0,1,0}, rotation), x = rotate({1,0,0}, rotation), z = rotate({0,0,1}, rotation);
    const auto top = center + up * (height * .5f - radius), bottom = center - up * (height * .5f - radius);
    circle(top, x, z, radius); circle(bottom, x, z, radius);
    for (auto side : {x, x * -1, z, z * -1}) line(top + side * radius, bottom + side * radius);
    for (auto side : {x, z}) for (int i = 0; i < 16; ++i) {
        const float a = pi * i / 16, b = pi * (i + 1) / 16;
        line(top + (side * std::cos(a) + up * std::sin(a)) * radius, top + (side * std::cos(b) + up * std::sin(b)) * radius);
        line(bottom + (side * std::cos(a) - up * std::sin(a)) * radius, bottom + (side * std::cos(b) - up * std::sin(b)) * radius);
    }
}
void Gizmos::frustum(const Transform& transform, const Camera& camera, float aspect) {
    if (!drawing) return;
    if (!std::isfinite(aspect) || aspect <= 0) throw std::invalid_argument("Camera gizmo aspect must be positive and finite");
    std::array<Vector3, 8> corners;
    for (int plane = 0; plane < 2; ++plane) {
        const float distance = plane ? camera.farPlane : camera.nearPlane;
        const float halfY = camera.orthographic ? camera.orthographicSize : distance * std::tan(camera.fieldOfView * .5f);
        for (int i = 0; i < 4; ++i) corners[plane * 4 + i] = transform.position() + rotate(
            {(i & 1 ? halfY : -halfY) * aspect, i & 2 ? halfY : -halfY, distance}, transform.rotation());
    }
    for (int i = 0; i < 8; ++i) for (int axis : {1,2,4}) if (!(i & axis)) line(corners[i], corners[i | axis]);
}
std::vector<GizmoLine> Gizmos::collect(Scene& scene, Entity* selected, float aspect, std::vector<std::string>* errors, GizmoOptions options) {
    std::vector<GizmoLine> result;
    if (!options.enabled) return result;
    struct Scope {
        std::vector<GizmoLine>* previous = drawing;
        Color previousColor = color;
        Scope(std::vector<GizmoLine>& lines) { drawing = &lines; }
        ~Scope() { drawing = previous; color = previousColor; }
    } scope(result);
    // Snapshot callbacks so appending components/objects does not invalidate the traversal.
    std::vector<std::pair<Entity*, MonoBehaviour*>> callbacks;
    if (options.scripts) for (auto& entity : scene.entities) for (auto& component : entity.components)
        if (auto* script = dynamic_cast<MonoBehaviour*>(component.get()); script && script->enabled) callbacks.emplace_back(&entity, script);
    const auto alive = [&](Entity* entity, Component* target) {
        for (auto& item : scene.entities) if (&item == entity)
            for (auto& component : item.components) if (component.get() == target) return true;
        return false;
    };
    for (auto [entity, script] : callbacks) {
        if (!alive(entity, script) || !script->enabled) continue;
        const auto ownerName = entity->name;
        Scene* previous = script->scene_; script->scene_ = &scene; color = {110,230,140};
        try { script->OnGizmos(); if (entity == selected && alive(entity, script)) script->OnGizmosSelected(); }
        catch (const std::exception& error) { if (errors) errors->push_back(ownerName + ": " + error.what()); }
        if (alive(entity, script)) script->scene_ = previous;
    }
    for (auto& entity : scene.entities) {
        if (options.selectedOnly && &entity != selected) continue;
        const auto& t = entity.transform; const auto scale = t.lossyScale();
        for (const auto& component : entity.components) if (component->enabled) {
            color = {110,230,140};
            if (const auto* box = dynamic_cast<const BoxCollider*>(component.get()); box && options.colliders) {
                auto half = absolute(t.vector({box->size.x * .5f,0,0})) + absolute(t.vector({0,box->size.y * .5f,0})) + absolute(t.vector({0,0,box->size.z * .5f}));
                wireCube(t.point(box->center), half * 2);
            } else if (const auto* sphere = dynamic_cast<const SphereCollider*>(component.get()); sphere && options.colliders) {
                wireSphere(t.point(sphere->center), sphere->radius * std::max({scale.x,scale.y,scale.z}));
            } else if (const auto* capsule = dynamic_cast<const CapsuleCollider*>(component.get()); capsule && options.colliders) {
                const float radius = capsule->radius * std::max(scale.x,scale.z);
                // Match Physics' round world caps and segment along the transformed local Y.
                const auto up = normalized(t.vector({0,1,0})); const auto axis = cross(Vector3{0,1,0}, up);
                const auto rotation = length(axis) < 1e-6f ? Quaternion{} : Quaternion::axisAngle(axis, std::acos(std::clamp(up.y,-1.f,1.f)));
                wireCapsule(t.point(capsule->center), radius, std::max(2 * radius, capsule->height * scale.y), rotation);
            } else if (const auto* camera = dynamic_cast<const Camera*>(component.get()); camera && options.cameras) {
                color = {245,200,90}; frustum(t, *camera, aspect);
            }
        }
    }
    return result;
}
} // namespace tiny3d
