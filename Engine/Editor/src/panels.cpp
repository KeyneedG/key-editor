#include "tiny3d/editor.hpp"
#include "tiny3d/json.hpp"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace tiny3d::editor {
namespace {
using J = json::Value;
namespace fs = std::filesystem;
std::string number(double v) { std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(7) << v; return s.str(); }
double number(const std::string& s) {
    double v = 0; auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || !std::isfinite(v)) throw std::runtime_error("Enter a finite number");
    return v;
}
J read(const fs::path& path) {
    std::ifstream in(path); if (!in) throw std::runtime_error("Cannot read project settings");
    return json::parse(std::string((std::istreambuf_iterator<char>(in)), {}));
}
}

void Editor::inspect(Entity& root) {
    if (!selected_ || *selected_ >= content_.entities.size()) { label(root, 0, 185, "Select an object"); return; }
    const auto selected = *selected_;
    const auto data = json::parse(current_);
    const auto object = data.at("entities").at(selected);
    // A single inspector path edits the JSON data used by scenes and undo snapshots.
    const auto editObject = [this, selected](const std::string& key, J value) {
        auto doc = json::parse(current_);
        std::get<J::Array>(doc["entities"].data).at(selected)[key] = std::move(value);
        auto source = json::stringify(doc); change([&](Scene& scene) { scene = deserializeScene(source); });
    };
    const auto editComponent = [this, selected](std::size_t i, const std::string& key, J value) {
        auto doc = json::parse(current_);
        auto& object = std::get<J::Array>(doc["entities"].data).at(selected);
        std::get<J::Array>(object["components"].data).at(i)[key] = std::move(value);
        auto source = json::stringify(doc); change([&](Scene& scene) { scene = deserializeScene(source); });
    };
    float contentHeight = 320;
    for (const auto& c : object.at("components").array()) {
        contentHeight += 66;
        for (const auto& [key, v] : c.object()) if (key != "type" && key != "enabled") contentHeight += key == "code" ? 150 : 34;
    }
    auto& list = scroll(root, 0, -10, 308, 450, contentHeight, 1);
    float y = 0;
    const auto textField = [&](std::string key, std::string value) {
        label(list, -105, y, key, 1.2f);
        field(list, 40, y, 192, value, [editObject, key](const std::string& v) { editObject(key, v); }); y -= 34;
    };
    textField("name", object.at("name").string()); textField("tag", object.at("tag").string());
    label(list, -105, y, "Layer", 1.2f);
    field(list, -13, y, 74, number(object.at("layer").number()), [editObject](const std::string& v) { editObject("layer", number(v)); });
    button(list, 92, y, 84, object.at("visible").boolean() ? "Visible" : "Hidden", [editObject, object] { editObject("visible", !object.at("visible").boolean()); }); y -= 34;
    label(list, -104, y, "Parent", 1.2f);
    field(list, 38, y, 194, object.at("parent").null() ? "-1" : number(object.at("parent").number()),
        [this, selected](const std::string& v) {
            double n = number(v); if (std::floor(n) != n || n < -1 || n >= double(content_.entities.size())) throw std::runtime_error("Parent is a hierarchy index, or -1 for none");
            change([&](Scene& scene) { scene.entities[selected].setParent(n == -1 ? nullptr : &scene.entities[static_cast<std::size_t>(n)]); });
        }); y -= 38;
    label(list, 0, y, "Transform (local / degrees)", 1.3f); y -= 28;
    for (auto key : {"position", "rotation", "scale"}) {
        label(list, -102, y, key, 1.1f);
        Vector3 value = key == std::string("position") ? content_.entities[selected].transform.localPosition :
            key == std::string("scale") ? content_.entities[selected].transform.localScale : content_.entities[selected].transform.localRotation.toEulerDegrees();
        for (int i = 0; i < 3; ++i) {
            float initial = i == 0 ? value.x : i == 1 ? value.y : value.z;
            field(list, -24 + i * 65.f, y, 60, number(double(initial)), [this, selected, key = std::string(key), i](const std::string& text) {
                float n = static_cast<float>(number(text));
                if (!std::isfinite(n)) throw std::runtime_error("Number is too large");
                change([&](Scene& scene) {
                    auto& t = scene.entities[selected].transform;
                    Vector3 v = key == "position" ? t.localPosition : key == "scale" ? t.localScale : t.localRotation.toEulerDegrees();
                    (i == 0 ? v.x : i == 1 ? v.y : v.z) = n;
                    if (key == "position") t.localPosition = v;
                    else if (key == "scale") t.localScale = v;
                    else t.localRotation = Quaternion::fromEulerDegrees(v);
                });
            });
        }
        y -= 34;
    }
    y -= 10;
    const auto& components = object.at("components").array();
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto& c = components[i]; const auto type = c.at("type").string();
        label(list, -22, y, type, 1.4f);
        button(list, 111, y, 46, c.at("enabled").boolean() ? "On" : "Off", [editComponent, i, c] { editComponent(i, "enabled", !c.at("enabled").boolean()); }); y -= 32;
        for (const auto& [key, value] : c.object()) {
            if (key == "type" || key == "enabled") continue;
            const auto set = [editComponent, i, key](J v) { editComponent(i, key, std::move(v)); };
            label(list, -93, y, key.size() > 13 ? key.substr(0, 13) : key, 1.05f);
            if (std::holds_alternative<bool>(value.data)) {
                button(list, 59, y, 150, value.boolean() ? "True" : "False", [set, value] { set(!value.boolean()); });
            } else if (std::holds_alternative<double>(value.data) || value.null()) {
                field(list, 59, y, 150, value.null() ? "-1" : number(value.number()), [set, key](const std::string& text) {
                    double n = number(text); set(n == -1 && (key == "mesh" || key == "camera" || key == "content") ? J{} : J(n));
                });
            } else if (std::holds_alternative<std::string>(value.data)) {
                if (key == "code") {
                    y -= 74; field(list, 0, y, 288, value.string(), [set](const std::string& text) { set(text); }, 116, true); y -= 62;
                } else field(list, 59, y, 150, value.string(), [set](const std::string& text) { set(text); });
            } else if (std::holds_alternative<J::Array>(value.data)) {
                for (std::size_t a = 0; a < value.array().size(); ++a) {
                    field(list, -20 + float(a) * 64, y, 60, number(value.at(a).number()), [this, selected, i, key, a](const std::string& text) {
                        auto doc = json::parse(current_); auto& c = std::get<J::Array>(std::get<J::Array>(doc["entities"].data).at(selected)["components"].data).at(i);
                        std::get<J::Array>(c[key].data).at(a) = number(text);
                        auto source = json::stringify(doc); change([&](Scene& scene) { scene = deserializeScene(source); });
                    });
                }
            }
            y -= 34;
        }
        if (type == "MeshRenderer") {
            for (int primitive = 0; primitive < 2; ++primitive) button(list, -76 + primitive * 150.f, y, 140, primitive ? "Plane Mesh" : "Cube Mesh", [this, selected, i, primitive] {
                auto doc = json::parse(current_); auto& meshes = std::get<J::Array>(doc["meshes"].data);
                Mesh mesh = primitive ? plane() : cube(); J::Array vertices, triangles;
                for (auto v : mesh.vertices) vertices.push_back(J::Array{v.x, v.y, v.z});
                for (auto t : mesh.triangles) triangles.push_back(J::Array{t[0], t[1], t[2]});
                auto& component = std::get<J::Array>(std::get<J::Array>(doc["entities"].data).at(selected)["components"].data).at(i);
                component["mesh"] = meshes.size(); meshes.push_back(J::Object{{"vertices", vertices}, {"triangles", triangles}});
                auto source = json::stringify(doc); change([&](Scene& scene) { scene = deserializeScene(source); });
            });
            y -= 34;
        }
        button(list, 0, y, 280, "Remove " + type, [this, selected, i] {
            auto doc = json::parse(current_); auto& components = std::get<J::Array>(std::get<J::Array>(doc["entities"].data).at(selected)["components"].data);
            components.erase(components.begin() + i); auto source = json::stringify(doc);
            change([&](Scene& scene) { scene = deserializeScene(source); });
        }); y -= 44;
    }
    button(list, 0, y, 280, "+ Add Component", [this] { dialog_ = Dialog::AddComponent; rebuild_ = true; });
    scrolls_[1]->getComponent<ScrollRect>()->contentHeight = -y + 50;
}

void Editor::dialog(Entity& root) {
    if (dialog_ == Dialog::None) return;
    // An interactable backdrop blocks picking through the modal, using the same UI.
    auto& backdrop = panel(root, 0, 0, 1280, 800, {17, 22, 30}); backdrop.transform.localPosition.z = -5;
    auto& blocker = backdrop.addComponent<Button>(); blocker.normalColor = blocker.hoverColor = blocker.pressedColor = {17, 22, 30};
    auto& box = panel(backdrop, 0, 0, 570, dialog_ == Dialog::AddComponent ? 560 : 380, {35, 43, 57});
    const auto close = [this] { dialog_ = Dialog::None; pending_ = {}; rebuild_ = true; };
    if (dialog_ == Dialog::NewProject || dialog_ == Dialog::NewScene) {
        const bool isProject = dialog_ == Dialog::NewProject;
        label(box, 0, 140, isProject ? "New Project" : "New Scene", 2.3f);
        label(box, 0, 92, isProject ? "Name (inside Engine/Projects)" : "Scene name (inside Assets)", 1.5f);
        auto& input = field(box, 0, 40, 480, dialogText_, [this](const std::string& text) { dialogText_ = text; });
        // The callback reads the field too, so Create works without pressing Enter first.
        button(box, -126, -45, 230, "Create", [this, isProject, input = &input] {
            const auto name = input->getComponent<InputField>()->text;
            if (isProject) newProject(name); else newScene(name); rebuild_ = true;
        });
        button(box, 126, -45, 230, "Cancel", close);
        label(box, 0, -138, "Enter commits input | Escape cancels input", 1.2f);
    } else if (dialog_ == Dialog::Unsaved) {
        label(box, 0, 110, "Unsaved scene changes", 2);
        label(box, 0, 50, "Save before continuing?", 1.7f);
        button(box, -176, -28, 160, "Save", [this] { save(); auto action = std::move(pending_); dialog_ = Dialog::None; if (action) action(); rebuild_ = true; });
        button(box, 0, -28, 160, "Discard", [this] { auto action = std::move(pending_); dialog_ = Dialog::None; if (action) action(); rebuild_ = true; });
        button(box, 176, -28, 160, "Cancel", close);
    } else if (dialog_ == Dialog::About) {
        label(box, 0, 120, "EryEngine", 3);
        label(box, 0, 42, "Small C++ engine and scene editor\nSoftware renderer | EryScript\nBuilt with the engine UI framework", 1.7f);
        button(box, 0, -114, 260, "Close", close);
    } else if (dialog_ == Dialog::AddObject || dialog_ == Dialog::AddComponent) {
        const bool object = dialog_ == Dialog::AddObject;
        label(box, 0, object ? 140 : 240, object ? "Create Object" : "Add Component", 2);
        const std::vector<std::string> types = object ? std::vector<std::string>{"Empty", "Cube", "Plane", "Camera"} :
            std::vector<std::string>{"MeshRenderer", "Camera", "BoxCollider", "SphereCollider", "CapsuleCollider", "Rigidbody", "EryScript", "Canvas", "Image", "SimpleText", "Button", "ScrollRect", "InputField"};
        for (std::size_t i = 0; i < types.size(); ++i) {
            const auto type = types[i]; button(box, i % 2 ? 136 : -136, (object ? 70.f : 172.f) - float(i / 2) * 46, 248, type, [this, object, type] {
                if (object) addObject(type); else if (selected_) addComponent(*selected_, type); dialog_ = Dialog::None; rebuild_ = true;
            });
        }
        button(box, 0, object ? -130 : -238, 260, "Cancel", close);
    } else if (dialog_ == Dialog::BuildSettings) {
        label(box, 0, 140, "Build Settings", 2.2f);
        if (project_.empty()) { label(box, 0, 20, "Create or open a project first"); button(box, 0, -130, 260, "Close", close); return; }
        const auto settings = read(project_ / "project.json");
        label(box, -180, 88, "Size", 1.4f);
        auto& width = field(box, -55, 88, 160, settings.has("width") ? number(settings.at("width").number()) : "1280", [](const std::string&) {});
        auto& height = field(box, 125, 88, 160, settings.has("height") ? number(settings.at("height").number()) : "800", [](const std::string&) {});
        label(box, -160, 37, "Startup Scene", 1.3f);
        auto& startup = field(box, 60, 37, 300, settings.has("startupScene") ? settings.at("startupScene").string() :
            scenePath_.empty() ? "" : fs::relative(scenePath_, project_).generic_string(), [](const std::string&) {});
        auto& fullscreen = button(box, 0, -14, 460, settings.has("fullscreen") && settings.at("fullscreen").boolean() ? "Fullscreen: True" : "Fullscreen: False", [] {});
        fullscreen.getComponent<Button>()->onClick = [p = &fullscreen] { auto& text = p->getChild(0)->getComponent<SimpleText>()->text; text = text.ends_with("True") ? "Fullscreen: False" : "Fullscreen: True"; };
        button(box, -130, -72, 238, "Save Settings", [this, width = &width, height = &height, startup = &startup, fullscreen = &fullscreen] {
            auto settings = read(project_ / "project.json");
            double w = number(width->getComponent<InputField>()->text), h = number(height->getComponent<InputField>()->text);
            if (std::floor(w) != w || std::floor(h) != h || w < 320 || h < 200 || w > 7680 || h > 4320) throw std::runtime_error("Resolution must be 320x200 through 7680x4320");
            fs::path path = project_ / startup->getComponent<InputField>()->text;
            auto relative = fs::relative(fs::weakly_canonical(path), fs::weakly_canonical(assets_));
            if (relative.empty() || *relative.begin() == ".." || !fs::is_regular_file(path)) throw std::runtime_error("Select a saved startup scene inside Assets");
            loadScene(path); settings["width"] = w; settings["height"] = h;
            settings["startupScene"] = fs::relative(path, project_).generic_string();
            settings["fullscreen"] = fullscreen->getChild(0)->getComponent<SimpleText>()->text.ends_with("True");
            std::ofstream out(project_ / "project.json"); out << json::stringify(settings) << '\n'; out.close();
            if (!out) throw std::runtime_error("Cannot save build settings");
            dialog_ = Dialog::None; status_ = "Build settings saved. Set engine.json mode to game and rebuild to run."; rebuild_ = true;
        });
        button(box, 130, -72, 238, "Cancel", close);
        label(box, 0, -138, "Game mode uses these settings on the next build", 1.2f);
    }
    label(box, 0, dialog_ == Dialog::AddComponent ? -204 : -174, status_.substr(0, 65), 1.1f);
}
} // namespace tiny3d::editor
