#include "tiny3d/editor.hpp"
#include <map>

namespace tiny3d::editor {
namespace {
bool same(Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
void appendLine(Mesh& mesh, const GizmoLine& line) {
    auto direction = line.to - line.from; const float size = length(direction);
    if (size < 1e-6f) return;
    direction = direction * (1 / size); auto axis = cross(Vector3{0,1,0}, direction);
    Quaternion rotation = length(axis) < 1e-6f ? Quaternion{} : Quaternion::axisAngle(axis, std::acos(std::clamp(direction.y, -1.f, 1.f)));
    const auto box = cube(); const auto first = mesh.vertices.size();
    for (auto p : box.vertices) mesh.vertices.push_back((line.from + line.to) * .5f + rotate({p.x * .018f, p.y * size, p.z * .018f}, rotation));
    for (auto t : box.triangles) mesh.triangles.push_back({first + t[0], first + t[1], first + t[2]});
}
}
void Editor::resizeLayout(const Input& input) {
    if (input.renderWidth <= 0 || input.renderHeight <= 0) return;
    const float aspect = float(input.renderWidth) / input.renderHeight;
    const float height = std::max(800.f, 1280 / aspect), width = height * aspect;
    if (std::abs(width - layoutWidth_) < .01f && std::abs(height - layoutHeight_) < .01f) return;
    layoutWidth_ = width; layoutHeight_ = height;
    if (playing()) { ui_.cancel(); buildPlayControls(); refreshPlayView(); }
    else { rebuild_ = true; rebuild(); }
}
void Editor::refreshComponentGizmos() {
    std::vector<std::string> errors;
    auto lines = Gizmos::collect(content_, selected_ && *selected_ < content_.entities.size() ? &content_.entities[*selected_] : nullptr, layoutWidth_ / layoutHeight_, &errors, gizmoOptions_);
    if (!errors.empty()) status_ = "Gizmo: " + errors.front();
    const bool unchanged = lines.size() == gizmoLines_.size() && std::equal(lines.begin(), lines.end(), gizmoLines_.begin(),
        [](const auto& a, const auto& b) { return same(a.from,b.from) && same(a.to,b.to) && a.color.packed() == b.color.packed(); });
    if (unchanged) return;
    gizmoLines_ = std::move(lines);
    std::map<std::uint32_t, std::shared_ptr<Mesh>> meshes;
    for (const auto& line : gizmoLines_) {
        auto& mesh = meshes[line.color.packed()]; if (!mesh) mesh = std::make_shared<Mesh>(); appendLine(*mesh, line);
    }
    std::size_t i = 0;
    for (const auto& [color, mesh] : meshes) {
        if (i == componentGizmos_.size()) {
            auto& entity = view_.entities.emplace_back(); entity.layer = 30; entity.name = "Editor Gizmos";
            entity.addComponent<MeshRenderer>().unlit = true; componentGizmos_.push_back(&entity);
        }
        auto& entity = *componentGizmos_[i++]; entity.visible = true;
        auto* visual = entity.getComponent<MeshRenderer>(); visual->mesh = mesh;
        visual->color = {std::uint8_t(color >> 16), std::uint8_t(color >> 8), std::uint8_t(color)};
    }
    for (; i < componentGizmos_.size(); ++i) componentGizmos_[i]->visible = false;
}
} // namespace tiny3d::editor
