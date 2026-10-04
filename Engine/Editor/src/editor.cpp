#include "tiny3d/editor.hpp"
#include "tiny3d/json.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/eryscript.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cctype>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tiny3d::editor {
namespace {
namespace fs = std::filesystem;
using J = json::Value;
using O = J::Object;
const Color accent{68, 102, 153}, panelColor{29, 34, 44};
J readJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path.string());
    return json::parse(std::string((std::istreambuf_iterator<char>(in)), {}));
}
void writeJson(const fs::path& path, const J& value) {
    const auto data = json::stringify(value); auto tmp = path; tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << data << '\n'; out.close();
    if (!out) throw std::runtime_error("Cannot write " + path.string());
#if defined(_WIN32)
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace " + path.string());
#else
    fs::rename(tmp, path);
#endif
}
void validateName(const std::string& name) {
    if (name.empty() || name.size() > 100 || name == "." || name == ".." || name.back() == ' ' || name.back() == '.' ||
        name.find_first_of("<>:\"/\\|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
        throw std::runtime_error("Use a folder/file name without path separators or reserved characters");
    std::string base = name.substr(0, name.find('.'));
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL" ||
        (base.size() == 4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3] >= '1' && base[3] <= '9'))
        throw std::runtime_error("That name is reserved by Windows");
}
bool inside(const fs::path& path, const fs::path& directory) {
    auto relative = fs::relative(fs::weakly_canonical(path), fs::weakly_canonical(directory));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
std::string shortText(std::string text, std::size_t limit = 28) {
    if (text.size() > limit) text = text.substr(0, limit - 3) + "...";
    return text;
}
const std::array<Vector3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
const std::array<Color, 3> colors{{{236, 80, 80}, {88, 208, 112}, {88, 148, 246}}};
float& axis(Vector3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }
bool project(Vector3 world, const Transform& camera, Vector3& screen) {
    Vector3 v = inverseRotate(world - camera.position(), camera.rotation());
    if (v.z <= .1f) return false;
    const float halfY = std::tan(pi / 6), halfX = halfY * 1280 / 800;
    screen = {(v.x / (v.z * halfX) + 1) * 640, (1 - v.y / (v.z * halfY)) * 400, v.z}; return true;
}
}

Editor::Editor(Scene initial, fs::path engineDirectory, fs::path projectDirectory, fs::path configFile)
    : content_(std::move(initial)), engine_(fs::absolute(engineDirectory)), config_(std::move(configFile)), project_(std::move(projectDirectory)) {
    fs::create_directories(engine_ / "Projects");
    camera_.localPosition = {0, 3, -9}; camera_.localRotation = Quaternion::fromEuler({.18f, 0, 0});
    if (!project_.empty()) {
        project_ = fs::absolute(project_); assets_ = project_ / "Assets";
        folder_ = fs::exists(assets_) ? assets_ : project_;
        const auto manifest = project_ / "project.json";
        if (fs::exists(manifest)) {
            const auto settings = readJson(manifest);
            if (settings.has("startupScene")) {
                const auto path = project_ / settings.at("startupScene").string();
                if (!inside(path, project_)) throw std::runtime_error("Startup scene must be inside the project");
                content_ = loadScene(path); scenePath_ = path;
            }
        }
    }
    for (std::size_t i = 0; i < content_.entities.size(); ++i) {
        auto& e = content_.entities[i];
        if (e.name == "Object") e.name = e.tag != "Untagged" ? e.tag : "Object " + std::to_string(i + 1);
    }
    current_ = saved_ = serializeScene(content_);
    rebuild();
}
std::string Editor::title() const {
    return "EryEngine | " + (project_.empty() ? "No project" : project_.filename().string()) + " | " +
        (scenePath_.empty() ? "Untitled" : scenePath_.filename().string()) + (dirty() ? " *" : "");
}
void Editor::guard(const std::function<void()>& action) {
    try { action(); } catch (const std::exception& e) { status_ = e.what(); rebuild_ = true; }
}
void Editor::request(const std::function<void()>& action) {
    menu_.clear();
    if (dirty()) { pending_ = action; dialog_ = Dialog::Unsaved; rebuild_ = true; }
    else action();
}
void Editor::replace(Scene scene, const fs::path& path) {
    ui_.cancel(); scrolls_.fill(nullptr);
    content_ = std::move(scene); scenePath_ = path; current_ = saved_ = serializeScene(content_);
    undo_.clear(); redo_.clear(); selected_.reset(); offsets_.fill(0); dragging_ = -1; rebuild_ = true;
}
void Editor::change(const std::function<void(Scene&)>& action) {
    const std::string before = current_;
    try { action(content_); current_ = serializeScene(content_); deserializeScene(current_); }
    catch (...) { content_ = deserializeScene(before); rebuild_ = true; throw; }
    if (before != current_) {
        undo_.push_back(before); if (undo_.size() > 64) undo_.erase(undo_.begin()); redo_.clear();
    }
    rebuild_ = true;
}
void Editor::undo() {
    if (undo_.empty()) return;
    auto next = deserializeScene(undo_.back()); redo_.push_back(current_); current_ = undo_.back(); undo_.pop_back();
    content_ = std::move(next); if (selected_ && *selected_ >= content_.entities.size()) selected_.reset(); rebuild_ = true;
}
void Editor::redo() {
    if (redo_.empty()) return;
    auto next = deserializeScene(redo_.back()); undo_.push_back(current_); current_ = redo_.back(); redo_.pop_back();
    content_ = std::move(next); rebuild_ = true;
}
void Editor::rememberProject() {
    if (config_.empty()) return;
    auto settings = fs::exists(config_) ? readJson(config_) : J(O{{"mode", "editor"}, {"autoStart", true}});
    settings["project"] = fs::relative(project_, config_.parent_path()).generic_string(); writeJson(config_, settings);
}
void Editor::newProject(const std::string& name) {
    validateName(name); const auto target = engine_ / "Projects" / name;
    if (fs::exists(target)) throw std::runtime_error("A project with that name already exists");
    fs::create_directories(target / "Assets");
    const auto source = fs::is_directory(assets_) ? assets_ : project_;
    if (!source.empty() && fs::is_directory(source)) {
        for (fs::recursive_directory_iterator it(source), end; it != end; ++it) {
            const auto filename = it->path().filename().string();
            if (it->is_symlink() || filename == "build" || filename == ".git" || filename == ".vs" || filename == "Projects") {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }
            const auto dest = target / "Assets" / fs::relative(it->path(), source);
            if (it->is_directory()) fs::create_directories(dest);
            else if (it->is_regular_file()) fs::copy_file(it->path(), dest);
        }
    }
    // The new project is scene-based and can run without writing C++ source.
    auto firstScene = target / "Assets" / "Main.scene.json";
    for (int n = 2; fs::exists(firstScene); ++n) firstScene = target / "Assets" / ("Main" + std::to_string(n) + ".scene.json");
    saveScene(content_, firstScene);
    writeJson(target / "project.json", O{{"sources", J::Array{}}, {"startupScene", fs::relative(firstScene, target).generic_string()},
        {"width", 1280}, {"height", 800}, {"fullscreen", false}});
    project_ = target; assets_ = folder_ = target / "Assets";
    replace(deserializeScene(current_), firstScene); rememberProject(); dialog_ = Dialog::None; status_ = "Created project " + name;
}
void Editor::openProject(const fs::path& directory) {
    const auto target = fs::canonical(directory);
    if (!fs::is_directory(target / "Assets")) throw std::runtime_error("Select a project folder containing Assets");
    const auto manifest = readJson(target / "project.json");
    Scene next; fs::path firstScene;
    if (manifest.has("startupScene")) firstScene = target / manifest.at("startupScene").string();
    else {
        for (const auto& item : fs::recursive_directory_iterator(target / "Assets"))
            if (item.is_regular_file() && item.path().filename().string().ends_with(".scene.json")) { firstScene = item.path(); break; }
    }
    if (!firstScene.empty()) {
        if (!inside(firstScene, target / "Assets")) throw std::runtime_error("Startup scene must be inside Assets");
        next = loadScene(firstScene);
    }
    project_ = target; assets_ = folder_ = target / "Assets"; replace(std::move(next), firstScene);
    rememberProject(); dialog_ = Dialog::None; status_ = "Opened " + target.filename().string();
}
void Editor::newScene(const std::string& name) {
    if (project_.empty()) throw std::runtime_error("Create or open a project first");
    validateName(name); fs::create_directories(assets_);
    const auto path = assets_ / (name.ends_with(".json") ? name : name + ".scene.json");
    if (fs::exists(path)) throw std::runtime_error("A scene with that name already exists");
    Scene next; auto& camera = next.entities.emplace_back(); camera.name = "Camera";
    camera.transform.localPosition = {0, 2, -6}; camera.addComponent<Camera>();
    saveScene(next, path); replace(std::move(next), path); folder_ = assets_; dialog_ = Dialog::None; status_ = "Created " + path.filename().string();
}
void Editor::openScene(const fs::path& path) {
    if (!inside(path, assets_)) throw std::runtime_error("Select a scene inside the project's Assets folder");
    auto next = loadScene(path); replace(std::move(next), path); status_ = "Opened " + path.filename().string();
}
void Editor::save() {
    if (project_.empty()) throw std::runtime_error("Create or open a project first");
    fs::create_directories(assets_);
    if (scenePath_.empty()) {
        scenePath_ = assets_ / "Untitled.scene.json";
        for (int n = 2; fs::exists(scenePath_); ++n) scenePath_ = assets_ / ("Untitled" + std::to_string(n) + ".scene.json");
    }
    saveScene(content_, scenePath_); saved_ = current_; status_ = "Saved " + scenePath_.filename().string();
    auto manifest = readJson(project_ / "project.json");
    if (!manifest.has("startupScene")) { manifest["startupScene"] = fs::relative(scenePath_, project_).generic_string(); writeJson(project_ / "project.json", manifest); }
    rebuild_ = true;
}
std::size_t Editor::addObject(const std::string& kind) {
    const std::size_t i = content_.entities.size();
    change([&](Scene& scene) {
        auto& e = scene.entities.emplace_back(); e.name = kind;
        if (kind == "Cube" || kind == "Plane") e.addComponent<MeshRenderer>(std::make_shared<Mesh>(kind == "Cube" ? cube() : plane()), Color{180, 192, 216});
        else if (kind == "Camera") { e.addComponent<Camera>(); e.transform.localPosition = camera_.position(); e.transform.localRotation = camera_.rotation(); }
        else if (kind != "Empty") throw std::runtime_error("Unknown object type");
    });
    selected_ = i; offsets_[1] = 0;
    if (scrolls_[1]) scrolls_[1]->getComponent<ScrollRect>()->offset = 0;
    return i;
}
void Editor::addComponent(std::size_t object, const std::string& type) {
    change([&](Scene& scene) {
        auto& e = scene.entities.at(object);
        if (type == "MeshRenderer") e.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()), Color{180, 192, 216});
        else if (type == "Camera") e.addComponent<Camera>();
        else if (type == "BoxCollider") e.addComponent<BoxCollider>();
        else if (type == "SphereCollider") e.addComponent<SphereCollider>();
        else if (type == "CapsuleCollider") e.addComponent<CapsuleCollider>();
        else if (type == "Rigidbody") e.addComponent<Rigidbody>();
        else if (type == "EryScript") e.addComponent<EryScript>("using tiny3d\n\nmethod Update\nmend\n");
        else if (type == "Canvas") { auto& c = e.addComponent<Canvas>(); for (auto& camera : scene.entities) if (camera.getComponent<Camera>()) { c.camera = &camera; break; } }
        else if (type == "Image") e.addComponent<Image>();
        else if (type == "SimpleText") e.addComponent<SimpleText>().text = "Text";
        else if (type == "Button") { if (!e.getComponent<Image>()) e.addComponent<Image>(); e.addComponent<Button>(); }
        else if (type == "InputField") { if (!e.getComponent<Image>()) e.addComponent<Image>(); e.addComponent<InputField>(); }
        else if (type == "ScrollRect") {
            if (!e.getComponent<Image>()) e.addComponent<Image>();
            auto& s = e.addComponent<ScrollRect>(); auto& child = scene.entities.emplace_back(); child.name = "Content";
            child.setParent(&e, false); s.content = &child;
        } else throw std::runtime_error("Unknown component type");
    });
    dialog_ = Dialog::None;
}

Entity& Editor::element(Entity& parent, float x, float y, float z) {
    auto& e = view_.entities.emplace_back(); e.layer = 31; e.setParent(&parent, false); e.transform.localPosition = {x, y, z}; return e;
}
Entity& Editor::label(Entity& parent, float x, float y, std::string text, float size) {
    auto& e = element(parent, x, y); auto& t = e.addComponent<SimpleText>(); t.text = std::move(text); t.pixelSize = size; return e;
}
Entity& Editor::panel(Entity& parent, float x, float y, float width, float height, Color color) {
    auto& e = element(parent, x, y); auto& image = e.addComponent<Image>(); image.width = width; image.height = height; image.color = color; return e;
}
Entity& Editor::button(Entity& parent, float x, float y, float width, std::string text, std::function<void()> action) {
    auto& e = panel(parent, x, y, width, 28); e.addComponent<Button>().onClick = [this, action] { guard(action); };
    label(e, 0, 0, std::move(text), 1.35f); return e;
}
Entity& Editor::field(Entity& parent, float x, float y, float width, std::string value,
                      std::function<void(const std::string&)> submit, float height, bool multiline) {
    auto& e = panel(parent, x, y, width, height, {20, 25, 34}); auto& f = e.addComponent<InputField>();
    f.text = std::move(value); f.multiline = multiline; f.onSubmit = [this, submit](const std::string& s) { guard([&] { submit(s); }); }; return e;
}
Entity& Editor::scroll(Entity& parent, float x, float y, float width, float height, float contentHeight, int slot) {
    auto& e = panel(parent, x, y, width, height); auto& s = e.addComponent<ScrollRect>();
    auto& content = element(e, 0, height * .5f - 18); s.content = &content; s.contentHeight = contentHeight; s.offset = offsets_[slot];
    scrolls_[slot] = &e; return content;
}

void Editor::rebuild() {
    std::optional<Vector3> focusPosition;
    std::string focusText;
    if (auto* focused = ui_.focusedEntity()) { focusPosition = focused->transform.position(); focusText = focused->getComponent<InputField>()->text; }
    for (int i = 0; i < 3; ++i) if (scrolls_[i]) offsets_[i] = scrolls_[i]->getComponent<ScrollRect>()->offset;
    ui_.cancel(); view_ = deserializeScene(current_); scrolls_.fill(nullptr); handles_.fill(nullptr);
    // Disable scene cameras and interactions in the editor's presentation copy.
    for (auto& e : view_.entities) {
        e.layer = 0;
        for (auto& c : e.components) {
            if (dynamic_cast<Camera*>(c.get()) || dynamic_cast<Button*>(c.get()) || dynamic_cast<InputField*>(c.get())) c->enabled = false;
        }
    }
    auto& camera = view_.entities.emplace_back(); camera.transform = camera_; camera.addComponent<Camera>().layers = layerMask(0);
    viewportCamera_ = &camera;
    auto& gizmoCamera = view_.entities.emplace_back(); gizmoCamera.transform = camera_;
    auto& gc = gizmoCamera.addComponent<Camera>(); gc.layers = layerMask(30); gc.depth = 1; gc.clearColor = false;
    gizmos();
    auto& uiCamera = view_.entities.emplace_back(); uiCamera.layer = 31;
    auto& uc = uiCamera.addComponent<Camera>(); uc.layers = layerMask(31); uc.depth = 2; uc.clearColor = false;
    uc.orthographic = true; uc.orthographicSize = 400; uc.farPlane = 100;
    auto& root = element(uiCamera, 0, 0, 10); root.addComponent<Canvas>().camera = &uiCamera;
    auto& top = panel(root, 0, 382, 1280, 36, {22, 27, 36});
    button(top, -594, 0, 76, "File", [this] { menu_ = menu_ == "File" ? "" : "File"; rebuild_ = true; });
    button(top, -510, 0, 76, "Edit", [this] { menu_ = menu_ == "Edit" ? "" : "Edit"; rebuild_ = true; });
    button(top, -426, 0, 76, "Help", [this] { menu_ = menu_ == "Help" ? "" : "Help"; rebuild_ = true; });
    label(top, 360, 0, "ERYENGINE EDITOR", 1.6f);
    auto& hierarchy = panel(root, -530, 110, 220, 508); label(hierarchy, -12, 235, "Hierarchy");
    button(hierarchy, 85, 235, 30, "+", [this] { dialog_ = Dialog::AddObject; rebuild_ = true; });
    auto& list = scroll(hierarchy, 0, -8, 212, 454, float(content_.entities.size() * 30 + 20), 0);
    std::vector<std::pair<std::size_t, int>> hierarchyRows;
    const auto children = [&](auto&& self, const Entity* parent, int depth) -> void {
        for (std::size_t i = 0; i < content_.entities.size(); ++i) if (content_.entities[i].parent() == parent) {
            hierarchyRows.push_back({i, depth}); self(self, &content_.entities[i], depth + 1);
        }
    };
    children(children, nullptr, 0);
    for (std::size_t row = 0; row < hierarchyRows.size(); ++row) {
        const auto [i, depth] = hierarchyRows[row];
        auto& b = button(list, 0, -float(row * 30), 204, std::string(std::min(depth, 4), '>') + std::to_string(i) + ": " + shortText(content_.entities[i].name, 18 - std::min(depth, 4)),
            [this, i] { selected_ = i; offsets_[1] = 0; if (scrolls_[1]) scrolls_[1]->getComponent<ScrollRect>()->offset = 0; rebuild_ = true; });
        b.getComponent<Button>()->normalColor = selected_ == i ? accent : panelColor;
    }
    auto& inspector = panel(root, 480, 110, 320, 508); label(inspector, 0, 235, "Inspector"); inspect(inspector);
    auto& tools = panel(root, -50, 344, 740, 36, {29, 34, 44});
    for (int i = 0; i < 3; ++i) {
        auto& b = button(tools, -315 + i * 106.f, 0, 100, i == 0 ? "Move W" : i == 1 ? "Rotate E" : "Scale R",
            [this, i] { tool_ = static_cast<Tool>(i); rebuild_ = true; });
        b.getComponent<Button>()->normalColor = int(tool_) == i ? accent : panelColor;
    }
    label(tools, 216, 0, "RMB LOOK | WASD | F FRAME", 1.1f);
    auto& project = panel(root, 0, -260, 1280, 230); projectPanel(project);
    auto& status = panel(root, 0, -387, 1280, 26, {22, 27, 36}); label(status, 0, 0, shortText(status_, 130), 1.3f);
    if (!menu_.empty()) {
        const float x = menu_ == "File" ? -520 : menu_ == "Edit" ? -436 : -352;
        auto& popup = panel(root, x, 280, 220, menu_ == "File" ? 150 : menu_ == "Edit" ? 116 : 50); popup.transform.localPosition.z = -4;
        const auto item = [&](int row, const std::string& title, std::function<void()> action) {
            button(popup, 0, (menu_ == "File" ? 54.f : menu_ == "Edit" ? 36.f : 0.f) - row * 34.f, 208, title,
                [this, action] { menu_.clear(); rebuild_ = true; action(); });
        };
        if (menu_ == "File") {
            item(0, "New Scene", [this] { request([this] { dialogText_.clear(); dialog_ = Dialog::NewScene; rebuild_ = true; }); });
            item(1, "New Project", [this] { request([this] { dialogText_.clear(); dialog_ = Dialog::NewProject; rebuild_ = true; }); });
            item(2, "Open Project", [this] { request([this] { const auto path = selectProjectFolder(engine_ / "Projects"); if (!path.empty()) openProject(path); }); });
            item(3, "Build Settings", [this] { dialog_ = Dialog::BuildSettings; rebuild_ = true; });
        } else if (menu_ == "Edit") {
            item(0, "Undo CTRL+Z", [this] { undo(); }); item(1, "Redo CTRL+Y", [this] { redo(); }); item(2, "Save CTRL+S", [this] { save(); });
        } else item(0, "About EryEngine", [this] { dialog_ = Dialog::About; rebuild_ = true; });
    }
    dialog(root); ui_.refresh(view_);
    if (focusPosition) for (auto& e : view_.entities) if (auto* field = e.getComponent<InputField>(); field && e.layer == 31) {
        if (length(e.transform.position() - *focusPosition) < .001f) { field->text = focusText; ui_.focus(e); break; }
    }
    rebuild_ = false;
}

void Editor::projectPanel(Entity& root) {
    label(root, -548, 96, "Project");
    label(root, 65, 96, project_.empty() ? "Create or open a project from File" : shortText(fs::relative(folder_, project_).generic_string(), 90), 1.3f);
    button(root, 578, 96, 94, "Refresh", [this] { rebuild_ = true; });
    if (project_.empty()) return;
    const auto browserRoot = fs::is_directory(assets_) ? assets_ : project_;
    std::vector<fs::directory_entry> entries;
    for (const auto& e : fs::directory_iterator(folder_)) if (!e.is_symlink() && e.path().filename() != "build" && e.path().filename() != ".vs") entries.push_back(e);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory()) return a.is_directory();
        return a.path().filename() < b.path().filename();
    });
    auto& list = scroll(root, 0, -10, 1252, 170, float(((entries.size() + 3) / 4 + 1) * 32), 2);
    if (folder_ != browserRoot) button(list, -465, 0, 298, ".. Parent folder", [this] { folder_ = folder_.parent_path(); offsets_[2] = 0; scrolls_[2]->getComponent<ScrollRect>()->offset = 0; rebuild_ = true; });
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto path = entries[i].path(); const bool directory = entries[i].is_directory();
        button(list, -465 + float(i % 4) * 310, -float(i / 4 + 1) * 32, 298,
            shortText((directory ? "[DIR] " : "") + path.filename().string(), 34), [this, path, directory] {
                if (clickedAsset_ == path && time_ - lastClick_ < .45f) {
                    if (directory) { folder_ = path; offsets_[2] = 0; scrolls_[2]->getComponent<ScrollRect>()->offset = 0; rebuild_ = true; }
                    else if (path.extension() == ".json") request([this, path] { openScene(path); });
                    else status_ = "Asset: " + path.filename().string();
                    clickedAsset_.clear();
                } else { clickedAsset_ = path; lastClick_ = time_; status_ = "Double-click folders or scene JSON to open"; }
                rebuild_ = true;
            });
    }
}

namespace {
Quaternion alignY(Vector3 direction) {
    direction = normalized(direction);
    if (direction.y < -.999f) return Quaternion::axisAngle({1, 0, 0}, pi);
    Vector3 c = cross({0, 1, 0}, direction); return normalized(Quaternion{c.x, c.y, c.z, 1 + direction.y});
}
void appendBox(Mesh& target, Vector3 center, Vector3 scale, Quaternion rotation = {}) {
    auto box = cube(); const auto first = target.vertices.size();
    for (auto v : box.vertices) target.vertices.push_back(center + rotate({v.x * scale.x, v.y * scale.y, v.z * scale.z}, rotation));
    for (auto t : box.triangles) target.triangles.push_back({first + t[0], first + t[1], first + t[2]});
}
float hitMesh(const Entity& entity, Vector3 origin, Vector3 direction) {
    float nearest = std::numeric_limits<float>::infinity();
    if (!entity.visibleInHierarchy()) return nearest;
    for (const auto& c : entity.components) {
        const auto* visual = dynamic_cast<const MeshRenderer*>(c.get());
        if (!visual || !visual->enabled || !visual->mesh) continue;
        for (auto t : visual->mesh->triangles) {
            auto a = entity.transform.point(visual->mesh->vertices.at(t[0]));
            auto b = entity.transform.point(visual->mesh->vertices.at(t[1]));
            auto c = entity.transform.point(visual->mesh->vertices.at(t[2]));
            auto e1 = b - a, e2 = c - a, p = cross(direction, e2); float determinant = dot(e1, p);
            if (std::abs(determinant) < 1e-7f) continue;
            auto s = origin - a; float u = dot(s, p) / determinant; if (u < 0 || u > 1) continue;
            auto q = cross(s, e1); float v = dot(direction, q) / determinant; if (v < 0 || u + v > 1) continue;
            float distance = dot(e2, q) / determinant; if (distance >= .1f) nearest = std::min(nearest, distance);
        }
    }
    return nearest;
}
}

void Editor::gizmos() {
    if (!selected_ || *selected_ >= content_.entities.size()) return;
    const auto& transform = content_.entities[*selected_].transform;
    Vector3 center = transform.position();
    const float size = std::max(.3f, length(center - camera_.position()) * .09f);
    for (int i = 0; i < 3; ++i) {
        Vector3 direction = tool_ == Tool::Scale ? rotate(axes[i], transform.rotation()) : axes[i];
        auto mesh = std::make_shared<Mesh>();
        if (tool_ == Tool::Rotate) {
            Vector3 u = axes[(i + 1) % 3], v = axes[(i + 2) % 3];
            for (int segment = 0; segment < 48; ++segment) {
                float a = segment * 2 * pi / 48, b = (segment + 1) * 2 * pi / 48;
                Vector3 p = (u * std::cos(a) + v * std::sin(a)) * size;
                Vector3 q = (u * std::cos(b) + v * std::sin(b)) * size;
                appendBox(*mesh, (p + q) * .5f, {size * .035f, length(q - p) + size * .02f, size * .035f}, alignY(q - p));
            }
        } else {
            appendBox(*mesh, direction * (size * .5f), {size * .035f, size, size * .035f}, alignY(direction));
            const float tip = tool_ == Tool::Scale ? .16f : .10f;
            appendBox(*mesh, direction * size, {size * tip, size * tip, size * tip});
        }
        auto& handle = view_.entities.emplace_back(); handle.layer = 30; handle.transform.localPosition = center;
        auto& visual = handle.addComponent<MeshRenderer>(mesh, colors[i]); visual.unlit = true; handles_[i] = &handle;
    }
}

void Editor::viewportInput(float seconds, const Input& input) {
    if (input.viewport.width <= 0 || input.viewport.height <= 0) return;
    Vector3 mouse{float(input.mouseX - input.viewport.x) * 1280 / input.viewport.width,
                 float(input.mouseY - input.viewport.y) * 800 / input.viewport.height, 0};
    bool over = mouse.x > 220 && mouse.x < 960 && mouse.y > 74 && mouse.y < 544;
    if (dragging_ >= 0) {
        if (!selected_) return;
        auto& t = content_.entities[*selected_].transform;
        const float amount = dot(mouse - dragMouse_, dragScreenAxis_);
        if (input.pressed(Key::Escape)) { content_ = deserializeScene(dragBefore_); current_ = dragBefore_; dragging_ = -1; rebuild_ = true; return; }
        if (tool_ == Tool::Move) t.setPosition(dragPosition_ + axes[dragging_] * (amount * dragUnits_));
        else if (tool_ == Tool::Rotate) t.setRotation(Quaternion::axisAngle(axes[dragging_], amount * .01f) * dragRotation_);
        else { t.localScale = dragScale_; axis(t.localScale, dragging_) = axis(dragScale_, dragging_) + amount * .01f; }
        current_ = serializeScene(content_); rebuild_ = true;
        if (!input.held(MouseButton::Left) || !input.focused) {
            if (current_ != dragBefore_) { undo_.push_back(dragBefore_); if (undo_.size() > 64) undo_.erase(undo_.begin()); redo_.clear(); }
            dragging_ = -1;
        }
        return;
    }
    if (!input.focused || dialog_ != Dialog::None || !menu_.empty() || ui_.editingText()) return;
    if (over && input.held(MouseButton::Right)) {
        auto euler = camera_.localRotation.toEuler();
        euler.y += input.mouseDeltaX * .003f; euler.x = std::clamp(euler.x + input.mouseDeltaY * .003f, -1.4f, 1.4f);
        camera_.localRotation = Quaternion::fromEuler(euler);
        const auto a = [&](Key positive, Key negative) { return float(input.held(positive)) - float(input.held(negative)); };
        camera_.localPosition = camera_.localPosition + rotate(Vector3{a(Key::D, Key::A), a(Key::E, Key::Q), a(Key::W, Key::S)}, camera_.rotation()) * (seconds * (input.held(Key::Shift) ? 18 : 6));
        rebuild_ = true;
    } else {
        if (input.pressed(Key::W) && !input.held(Key::Control)) { tool_ = Tool::Move; rebuild_ = true; }
        if (input.pressed(Key::E)) { tool_ = Tool::Rotate; rebuild_ = true; }
        if (input.pressed(Key::R)) { tool_ = Tool::Scale; rebuild_ = true; }
    }
    if (input.pressed(Key::F) && selected_) {
        const auto& t = content_.entities[*selected_].transform;
        const float distance = std::max(4.f, length(t.lossyScale()) * 2);
        camera_.localPosition = t.position() - camera_.forward() * distance; rebuild_ = true;
    }
    if (over && input.mouseWheel != 0) { camera_.localPosition = camera_.localPosition + camera_.forward() * input.mouseWheel; rebuild_ = true; }
    if (!over || !input.pressed(MouseButton::Left)) return;
    Vector3 direction = rotate(normalized(Vector3{(mouse.x / 640 - 1) * std::tan(pi / 6) * 1280 / 800,
        (1 - mouse.y / 400) * std::tan(pi / 6), 1}), camera_.rotation());
    const Vector3 origin = camera_.position();
    float closest = std::numeric_limits<float>::infinity(); int picked = -1;
    for (int i = 0; i < 3; ++i) if (handles_[i]) {
        float distance = hitMesh(*handles_[i], origin, direction); if (distance < closest) { closest = distance; picked = i; }
    }
    if (picked >= 0 && selected_) {
        const auto& t = content_.entities[*selected_].transform;
        if (content_.entities[*selected_].parent()) content_.entities[*selected_].parent()->transform.inversePoint(t.position());
        dragging_ = picked; dragBefore_ = current_; dragMouse_ = mouse;
        dragPosition_ = t.position(); dragRotation_ = t.rotation(); dragScale_ = t.localScale;
        Vector3 worldAxis = tool_ == Tool::Scale ? rotate(axes[picked], t.rotation()) : axes[picked];
        const float size = std::max(.3f, length(t.position() - origin) * .09f);
        Vector3 a, b;
        if (tool_ == Tool::Rotate) {
            Vector3 hit = origin + direction * closest;
            project(hit, camera_, a); project(hit + normalized(cross(worldAxis, hit - t.position())) * size, camera_, b);
        } else { project(t.position(), camera_, a); project(t.position() + worldAxis * size, camera_, b); }
        Vector3 screenAxis = b - a; screenAxis.z = 0;
        float screenLength = length(screenAxis);
        dragScreenAxis_ = screenLength < 5 ? Vector3{0, -1, 0} : normalized(screenAxis);
        dragUnits_ = screenLength < 5 ? .015f : size / screenLength;
    } else {
        std::optional<std::size_t> selection; closest = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < content_.entities.size(); ++i) {
            float distance = hitMesh(view_.entities[i], origin, direction);
            if (distance < closest) { closest = distance; selection = i; }
        }
        selected_ = selection; offsets_[1] = 0; if (scrolls_[1]) scrolls_[1]->getComponent<ScrollRect>()->offset = 0; rebuild_ = true;
    }
}

void Editor::update(float seconds, const Input& input) {
    time_ += seconds;
    guard([&] {
        const bool wasEditing = ui_.editingText();
        ui_.update(view_, input);
        // Retain pressed controls until release when committing a field changed the scene.
        if (rebuild_ && (!input.held(MouseButton::Left) || dragging_ >= 0)) rebuild();
        viewportInput(seconds, input);
        if (input.focused && !ui_.editingText() && dialog_ == Dialog::None && dragging_ < 0 && input.held(Key::Control)) {
            if (input.pressed(Key::S)) save();
            if (input.pressed(Key::Z)) { if (input.held(Key::Shift)) redo(); else undo(); }
            if (input.pressed(Key::Y)) redo();
        }
        if (input.pressed(Key::Escape) && !wasEditing && dragging_ < 0) { menu_.clear(); dialog_ = Dialog::None; pending_ = {}; rebuild_ = true; }
        if (rebuild_ && (!input.held(MouseButton::Left) || dragging_ >= 0)) rebuild();
    });
}
} // namespace tiny3d::editor
