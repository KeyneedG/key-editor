#include "tiny3d/editor.hpp"
#include "tiny3d/physics.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>

using namespace tiny3d;
namespace fs = std::filesystem;
void check(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
bool close(Vector3 a, Vector3 b) { return length(a - b) < .0001f; }
class DrawProbe final : public MonoBehaviour {
public:
    int awakes = 0, always = 0, selected = 0;
    void Awake() override { ++awakes; }
    void OnGizmos() override {
        ++always; check(scene().entities.size() > 0, "Gizmos have no scene context");
        Gizmos::color = {20,100,255}; Gizmos::line(transform().position(), transform().position() + Vector3{1,0,0});
    }
    void OnGizmosSelected() override { ++selected; Gizmos::color = {255,120,20}; Gizmos::wireSphere(transform().position(), 2); }
};
class BadGizmo final : public MonoBehaviour {
public:
    void OnGizmos() override { throw std::runtime_error("Expected gizmo error"); }
};
TINY3D_REGISTER_BEHAVIOUR(DrawProbe)
void geometryChecks() {
    Gizmos::line({}, {1,0,0}); Gizmos::wireSphere({},0); // Editor drawing is harmless outside callbacks.
    Scene scene; auto& parent = scene.createEntity("Parent"); parent.transform.localPosition = {3,4,5};
    parent.transform.localRotation = Quaternion::fromEuler({0,.7f,0}); parent.transform.localScale = {2,3,4};
    auto& entity = scene.createEntity("Collider", &parent); entity.transform.localPosition = {1,0,0};
    auto& box = entity.addComponent<BoxCollider>(); box.center = {.5f,0,0}; box.size = {2,4,6};
    auto lines = Gizmos::collect(scene, &entity, 1.6f); check(lines.size() == 12, "Box gizmo must have 12 world AABB edges");
    Vector3 low{1e9f,1e9f,1e9f}, high{-1e9f,-1e9f,-1e9f};
    for (auto line : lines) for (auto p : {line.from,line.to}) {
        low = {std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
        high = {std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
    }
    check(close((low + high) * .5f, entity.transform.point(box.center)), "Collider gizmo ignored its transformed center");
    check(std::abs(high.y - low.y - 12) < .001f && high.x - low.x > 10, "Box outline did not match the scaled/rotated physics bounds");
    box.enabled = false; entity.addComponent<SphereCollider>().radius = 2;
    lines = Gizmos::collect(scene, &entity, 1.6f); check(lines.size() == 96, "Sphere gizmo is missing its three rings");
    check(close(lines.front().from, entity.transform.position() + Vector3{8,0,0}), "Sphere gizmo did not use maximum world scale");
    entity.getComponent<SphereCollider>()->enabled = false; entity.addComponent<CapsuleCollider>();
    lines = Gizmos::collect(scene, &entity, 1.6f); check(lines.size() == 132, "Capsule gizmo is missing its caps or sides");
    entity.getComponent<CapsuleCollider>()->enabled = false; auto& camera = entity.addComponent<Camera>(); camera.nearPlane = .5f; camera.farPlane = 8;
    lines = Gizmos::collect(scene, &entity, 2); check(lines.size() == 12, "Camera frustum has incorrect edges");
    auto point = inverseRotate(lines.front().from - entity.transform.position(), entity.transform.rotation());
    check(std::abs(point.z - .5f) < .001f && std::abs(point.x / point.y - 2) < .001f, "Perspective camera gizmo ignored near/aspect");
    camera.orthographic = true; camera.orthographicSize = 4;
    lines = Gizmos::collect(scene, &entity, 2); point = inverseRotate(lines.front().from - entity.transform.position(), entity.transform.rotation());
    check(std::abs(point.x + 8) < .001f && std::abs(point.y + 4) < .001f, "Orthographic camera gizmo used perspective projection");
    camera.enabled = false; check(Gizmos::collect(scene, &entity, 2).empty(), "Disabled components still drew gizmos");
}
void filterChecks() {
    Scene scene; auto& collider = scene.createEntity(); collider.addComponent<BoxCollider>();
    scene.createEntity().addComponent<SphereCollider>(); scene.createEntity().addComponent<Camera>();
    auto& probe = collider.addComponent<DrawProbe>(); GizmoOptions options;
    check(Gizmos::collect(scene, nullptr, 1.6f, nullptr, options).size() == 1, "Default options drew unselected built-in shapes");
    options.selectedOnly = false;
    check(Gizmos::collect(scene, nullptr, 1.6f, nullptr, options).size() == 121, "All shapes option omitted unselected colliders/cameras");
    options.colliders = false;
    check(Gizmos::collect(scene, &collider, 1.6f, nullptr, options).size() == 109, "Collider filter affected script selection or camera shapes");
    options.cameras = false;
    check(Gizmos::collect(scene, &collider, 1.6f, nullptr, options).size() == 97, "Camera filter did not hide the frustum");
    options.scripts = false; const int calls = probe.always;
    check(Gizmos::collect(scene, &collider, 1.6f, nullptr, options).empty() && probe.always == calls, "Disabled script gizmos still ran callbacks");
    options = {}; options.enabled = false;
    check(Gizmos::collect(scene, &collider, 1.6f, nullptr, options).empty() && probe.always == calls, "Master switch still collected gizmos");
}
void callbackChecks(const fs::path& scratch) {
    Scene scene; auto& a = scene.createEntity(); auto& probe = a.addComponent<DrawProbe>();
    auto& bad = scene.createEntity().addComponent<BadGizmo>(); std::vector<std::string> errors;
    const auto color = Gizmos::color; auto lines = Gizmos::collect(scene, &a, 1.6f, &errors);
    check(lines.size() == 97 && probe.always == 1 && probe.selected == 1 && !probe.awakes, "Editor gizmos ran lifecycle or missed selection");
    check(errors.size() == 1 && bad.error().empty() && Gizmos::color.packed() == color.packed(), "Gizmo errors escaped isolation or color state leaked");
    probe.enabled = false; check(Gizmos::collect(scene, nullptr, 1.6f).empty(), "Disabled behaviour drew gizmos");
    editor::Editor editor({}, scratch); auto index = editor.addObject(); editor.addComponent(index, "C++: DrawProbe"); editor.addComponent(index, "MeshRenderer"); editor.addComponent(index, "BoxCollider"); editor.update(0, Input{});
    auto* original = editor.content().entities[index].getComponent<DrawProbe>();
    check(original && original->always && original->selected && !original->awakes, "Editor did not draw C++ gizmo callbacks before Awake");
    std::shared_ptr<const Mesh> mesh;
    for (auto& e : editor.scene().entities) if (e.name == "Editor Gizmos") mesh = e.getComponent<MeshRenderer>()->mesh;
    check(bool(mesh), "Gizmo drawing did not reach the editor renderer"); editor.update(.016f, Input{});
    bool retained = false; for (auto& e : editor.scene().entities) if (e.name == "Editor Gizmos" && e.getComponent<MeshRenderer>()->mesh == mesh) retained = true;
    check(retained, "Unchanged custom gizmos rebuilt geometry");
    Renderer image(1280,800); image.render(editor.scene()); image.savePPM((scratch / "gizmos.ppm").string());
    const auto before = serializeScene(editor.content()); editor.play(); editor.update(.016f, Input{});
    const auto* runtime = editor.playScene()->entities[index].getComponent<DrawProbe>();
    check(runtime->awakes == 1 && !runtime->always && !runtime->selected, "Editor gizmo callbacks ran in game mode");
    for (auto& e : editor.scene().entities) check(e.name != "Editor Gizmos", "Editor gizmos leaked into game rendering");
    std::function<void()> pause;
    for (auto& e : editor.scene().entities) if (auto* label = e.getComponent<SimpleText>(); label && label->text == "Pause") pause = e.parent()->getComponent<Button>()->onClick;
    check(bool(pause), "Play pause control is missing"); pause();
    Input resized; resized.renderWidth = 1920; resized.renderHeight = 1080; resized.viewport = {0,0,1920,1080}; editor.update(0,resized);
    bool resume = false; for (auto& e : editor.scene().entities) if (auto* label = e.getComponent<SimpleText>(); label && label->text == "Resume") resume = true;
    check(editor.paused() && resume, "Resizing paused play lost its Resume control");
    editor.stop(); check(serializeScene(editor.content()) == before, "Gizmos changed serialized scene state");
}
Entity* textEntity(Scene& scene, const std::string& text) {
    for (auto& e : scene.entities) if (auto* label = e.getComponent<SimpleText>(); label && label->text == text) return &e;
    return nullptr;
}
void optionsChecks(const fs::path& scratch) {
    Scene scene; auto& collider = scene.createEntity("Collider"); collider.addComponent<BoxCollider>(); collider.addComponent<DrawProbe>();
    auto& camera = scene.createEntity("Camera"); camera.transform.localPosition = {2,1,-2}; camera.addComponent<Camera>().farPlane = 5;
    editor::Editor editor(std::move(scene), scratch);
    const auto before = serializeScene(editor.content());
    const auto click = [&](const std::string& title) {
        auto* label = textEntity(editor.scene(), title); check(label && label->parent()->getComponent<Button>(), "Missing gizmo option");
        auto p = label->parent()->transform.position(); Input input; input.focused = true;
        input.renderWidth = 1280; input.renderHeight = 800; input.viewport = {0,0,1280,800};
        input.mouseX = int(640 + p.x); input.mouseY = int(400 - p.y); input.set(MouseButton::Left,true); editor.update(0,input);
        input.beginFrame(); input.set(MouseButton::Left,false); editor.update(0,input);
    };
    const auto visible = [&] { int count = 0; for (auto& e : editor.scene().entities) if (e.name == "Editor Gizmos" && e.visible) ++count; return count; };
    click("Gizmos");
    check(textEntity(editor.scene(), "Gizmo options") && textEntity(editor.scene(), "Colliders: On") && textEntity(editor.scene(), "Cameras: On") &&
        textEntity(editor.scene(), "Script gizmos: On") && textEntity(editor.scene(), "Shapes: Selected"), "Gizmo menu omitted its visibility options");
    click("Shapes: Selected"); check(visible() == 3, "All shapes did not display unselected built-in gizmos");
    click("Colliders: On"); check(visible() == 2 && textEntity(editor.scene(), "Colliders: Off"), "Collider option did not hide collider meshes");
    click("Cameras: On"); check(visible() == 1, "Camera option did not hide the frustum");
    click("Script gizmos: On"); check(visible() == 0, "Script option did not hide custom meshes");
    const int calls = editor.content().entities[0].getComponent<DrawProbe>()->always;
    editor.update(.016f,Input{}); check(editor.content().entities[0].getComponent<DrawProbe>()->always == calls, "Disabled script callbacks continued running");
    click("Colliders: Off"); click("Cameras: Off"); click("Script gizmos: Off");
    click("Enabled: On"); check(visible() == 0, "Master gizmo visibility switch did not hide meshes");
    click("Enabled: Off"); check(visible() == 3, "Reenabling gizmos lost category preferences");
    Renderer image(1280,800); image.render(editor.scene()); image.savePPM((scratch / "gizmo-options.ppm").string());
    click("X"); check(!textEntity(editor.scene(), "Gizmo options"), "Gizmo menu close button did not close it");
    check(!editor.dirty() && serializeScene(editor.content()) == before, "Gizmo preferences dirtied the scene");
}
void layoutChecks(const fs::path& scratch) {
    Scene scene; auto& parent = scene.createEntity("Parent"); auto& child = scene.createEntity("Child", &parent); scene.createEntity("Grandchild", &child);
    editor::Editor editor(std::move(scene), scratch);
    auto* p = textEntity(editor.scene(), "0: Parent"); auto* c = textEntity(editor.scene(), "1: Child"); auto* g = textEntity(editor.scene(), "2: Grandchild");
    check(p && c && g && !p->getComponent<SimpleText>()->centered && !c->getComponent<SimpleText>()->centered, "Hierarchy labels are still centered");
    check(std::abs(c->transform.position().x - p->transform.position().x - 12) < .001f &&
        std::abs(g->transform.position().x - c->transform.position().x - 12) < .001f, "Hierarchy depth has no visual indentation");
    for (auto size : {std::pair{1920,900}, {600,900}, {1600,600}}) {
        Input input; input.focused = true; input.renderWidth = size.first; input.renderHeight = size.second; input.viewport = {0,0,size.first,size.second};
        editor.update(0, input); auto* file = textEntity(editor.scene(), "File")->parent();
        const float aspect = float(size.first) / size.second, height = std::max(800.f,1280 / aspect), width = height * aspect;
        check(std::abs(file->transform.position().x + width * .5f - 46) < .01f, "Editor toolbar did not anchor to the resized left edge");
        auto* inspector = textEntity(editor.scene(), "Inspector")->parent();
        check(std::abs(inspector->transform.position().x - width * .5f + 160) < .01f &&
            std::abs(inspector->getComponent<Image>()->height - height + 292) < .01f, "Inspector did not adapt to new window dimensions");
        auto position = file->transform.position(); input.mouseX = int((position.x / width + .5f) * size.first);
        input.mouseY = int((.5f - position.y / height) * size.second); input.set(MouseButton::Left,true); editor.update(0,input);
        input.beginFrame(); input.set(MouseButton::Left,false); editor.update(0,input);
        check(textEntity(editor.scene(), "Open Project"), "Resized editor button picking differs from rendering");
        input.beginFrame(); input.set(Key::Escape,true); editor.update(0,input);
        Renderer renderer(size.first,size.second); renderer.render(editor.scene()); renderer.savePPM((scratch / ("layout-" + std::to_string(size.first) + ".ppm")).string());
    }
    std::cout << "Resize screenshots: " << scratch.string() << '\n';
}
void sourceChecks(const fs::path& scratch) {
    const auto path = scratch / "Test.ery"; std::ofstream(path) << "using tiny3d\nmethod Awake\n entity.name = \"From file\"\nmend\n";
    Scene scene; auto& script = scene.createEntity().addComponent<EryScript>(); script.source = path.string();
    auto serialized = serializeScene(scene); auto restored = deserializeScene(serialized);
    check(restored.entities.front().getComponent<EryScript>()->source == path.string(), "EryScript file reference was lost on save");
    Scripts scripts; scripts.awake(restored); auto* loaded = restored.entities.front().getComponent<EryScript>();
    check(loaded->error().empty() && restored.entities.front().name == "From file", "EryScript did not load its source file");
    std::ofstream(path) << "using tiny3d\nmethod Awake\n entity.name = \"Reloaded\"\nmend\n";
    loaded->restart(); scripts.awake(restored);
    check(loaded->error().empty() && restored.entities.front().name == "Reloaded", "Restart did not reread the EryScript file");
    loaded->source = (scratch / "Missing.ery").string(); scripts.awake(restored);
    check(!loaded->error().empty(), "Missing EryScript files fail silently");
}
int main() {
    try {
        const auto scratch = fs::current_path() / ("gizmo-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); fs::create_directories(scratch);
        geometryChecks(); filterChecks(); callbackChecks(scratch); optionsChecks(scratch); layoutChecks(scratch); sourceChecks(scratch);
        std::cout << "Collider/camera gizmos, callbacks, responsive layout, hierarchy, and EryScript source checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
