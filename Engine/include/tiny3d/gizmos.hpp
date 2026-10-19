#pragma once
#include "tiny3d/engine.hpp"

namespace tiny3d {
struct GizmoLine { Vector3 from{}, to{}; Color color{}; };
struct GizmoOptions {
    bool enabled = true, colliders = true, cameras = true, scripts = true;
    bool selectedOnly = true; // Applies to built-in shapes; script callbacks keep their selection semantics.
};
// Immediate editor drawing in world space. Calls outside a gizmo callback are harmless.
// Gizmos are presentation only: they are never saved, picked, or rendered in game mode.
class Gizmos {
public:
    inline static thread_local Color color{110, 230, 140};
    static void line(Vector3 from, Vector3 to);
    static void wireCube(Vector3 center, Vector3 size, Quaternion rotation = {});
    static void wireSphere(Vector3 center, float radius);
    static void wireCapsule(Vector3 center, float radius, float height, Quaternion rotation = {});
    static void frustum(const Transform& transform, const Camera& camera, float aspect);
    // Used by editor hosts; built-in shapes default to the selection and can be filtered.
    static std::vector<GizmoLine> collect(Scene& scene, Entity* selected, float aspect,
                                         std::vector<std::string>* errors = nullptr, GizmoOptions options = {});
};
} // namespace tiny3d
