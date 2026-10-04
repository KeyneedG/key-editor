#include "tiny3d/editor.hpp"
#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/json.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace tiny3d;
namespace fs = std::filesystem;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<class F> void rejects(F action) { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } check(rejected, "Invalid input was accepted"); }
bool close(Vector3 a, Vector3 b) { return length(a - b) < .0001f; }
Input pointer(int x, int y, int w = 1280, int h = 800) {
    Input i; i.focused = true; i.renderWidth = w; i.renderHeight = h; i.viewport = {0, 0, w, h}; i.mouseX = x; i.mouseY = y; return i;
}
void click(editor::Editor& editor, int x, int y) {
    auto i = pointer(x, y); i.set(MouseButton::Left, true); editor.update(.016f, i);
    i.beginFrame(); i.set(MouseButton::Left, false); editor.update(.016f, i);
}
Entity* findButton(Scene& scene, const std::string& text) {
    for (auto& e : scene.entities) if (e.getComponent<Button>()) for (std::size_t n = 0; n < e.childCount(); ++n)
        if (auto* t = e.getChild(n)->getComponent<SimpleText>(); t && t->text == text) return &e;
    return nullptr;
}
void clickButton(editor::Editor& editor, const std::string& name) {
    auto* b = findButton(editor.scene(), name); check(b, "Missing editor button");
    auto p = b->transform.position(); click(editor, int(640 + p.x), int(400 - p.y));
}

void sceneChecks() {
    Scene scene; scene.entities.resize(5);
    auto& parent = scene.entities[0]; parent.name = "Parent \"quoted\""; parent.tag = "test";
    parent.transform.localPosition = {2, 3, 4}; parent.transform.localScale = {2, 3, 4};
    parent.transform.localRotation = Quaternion::fromEuler({.2f, .4f, .6f});
    auto mesh = std::make_shared<Mesh>(cube());
    parent.addComponent<MeshRenderer>(mesh, Color{3, 4, 5}).unlit = true;
    parent.addComponent<BoxCollider>().center = {1, 2, 3}; parent.addComponent<SphereCollider>().radius = 2;
    parent.addComponent<CapsuleCollider>().height = 3; parent.addComponent<Rigidbody>().velocity = {4, 5, 6};
    parent.addComponent<EryScript>("using tiny3d\nmethod Update\nmend\n", false, true).enabled = false;
    auto& child = scene.entities[1]; child.setParent(&parent, false); child.transform.localPosition = {1, 2, 3};
    child.addComponent<MeshRenderer>(mesh); child.visible = false; child.layer = 7;
    scene.entities[2].addComponent<Camera>().depth = -1;
    auto& canvas = scene.entities[3]; canvas.addComponent<Canvas>().camera = &scene.entities[2];
    canvas.addComponent<Image>(); canvas.addComponent<Button>(); auto& scroll = canvas.addComponent<ScrollRect>();
    scroll.content = &scene.entities[4]; scroll.contentHeight = 400; scroll.offset = 80;
    scene.entities[4].setParent(&canvas, false); scene.entities[4].addComponent<SimpleText>().text = "hello\nworld";
    canvas.addComponent<InputField>().text = "unicode: \xc3\xa9";
    const auto childPosition = child.transform.position();
    UI ui; ui.refresh(scene); const auto encoded = serializeScene(scene);
    auto loaded = deserializeScene(encoded);
    check(loaded.entities[0].name == parent.name, "Names/escaping did not round-trip");
    check(loaded.entities[1].parent() == &loaded.entities[0], "Parent link lost");
    check(close(loaded.entities[1].transform.position(), childPosition), "World transform changed");
    check(!loaded.entities[1].visible && loaded.entities[1].layer == 7, "Metadata lost");
    check(loaded.entities[0].getComponent<MeshRenderer>()->mesh == loaded.entities[1].getComponent<MeshRenderer>()->mesh, "Shared mesh identity lost");
    check(loaded.entities[0].getComponent<EryScript>()->code == parent.getComponent<EryScript>()->code, "Script code lost");
    check(loaded.entities[3].getComponent<Canvas>()->camera == &loaded.entities[2], "Camera reference lost");
    ui.refresh(loaded);
    check(close(loaded.entities[4].transform.localPosition, scene.entities[4].transform.localPosition), "Scroll offset was applied twice on load");
    check(loaded.entities[3].getComponent<InputField>()->text == canvas.getComponent<InputField>()->text, "Input field lost");
    auto document = json::parse(encoded);
    std::get<json::Value::Array>(document["entities"].data)[0]["parent"] = 1;
    rejects([&] { deserializeScene(json::stringify(document)); });
    rejects([&] { deserializeScene("{\"version\":99}"); });
    rejects([&] { json::parse("[01]"); }); rejects([&] { json::parse("{\"x\":1,\"x\":2}"); });
    check(json::parse("\"\\uD83D\\uDE00\"").string().size() == 4, "Unicode JSON decoding failed");
}

void uiChecks() {
    Scene scene; scene.entities.resize(6);
    auto& camera = scene.entities[0]; auto& c = camera.addComponent<Camera>(); c.orthographic = true; c.orthographicSize = 100;
    auto& root = scene.entities[1]; root.addComponent<Canvas>().camera = &camera; root.transform.localPosition.z = 5;
    auto& rect = scene.entities[2]; rect.setParent(&root, false); auto& image = rect.addComponent<Image>(); image.width = 100; image.height = 60;
    auto& scroll = rect.addComponent<ScrollRect>(); scroll.contentHeight = 200;
    auto& content = scene.entities[3]; content.setParent(&rect, false); scroll.content = &content;
    auto& button = scene.entities[4]; button.setParent(&content, false); button.transform.localPosition = {0, -60, -.1f};
    button.addComponent<Image>().width = 80; button.getComponent<Image>()->height = 20;
    int clicked = 0; button.addComponent<Button>().onClick = [&] { ++clicked; };
    auto& field = scene.entities[5]; field.setParent(&root, false); field.transform.localPosition = {0, 65, -.1f};
    field.addComponent<Image>().width = 100; field.getComponent<Image>()->height = 28;
    auto& input = field.addComponent<InputField>(); input.text = "old"; std::string committed; input.onSubmit = [&](const std::string& s) { committed = s; };
    UI ui; ui.refresh(scene);
    check(button.getComponent<MeshRenderer>()->mesh->triangles.empty(), "Offscreen scroll content was not clipped");
    auto p = pointer(100, 160, 200, 200); p.set(MouseButton::Left, true); ui.update(scene, p); p.beginFrame(); p.set(MouseButton::Left, false); ui.update(scene, p);
    check(clicked == 0, "Clipped button received a click");
    p = pointer(100, 100, 200, 200); p.mouseWheel = -2; ui.update(scene, p);
    check(scroll.offset == 56, "Wheel did not move ScrollRect");
    check(!button.getComponent<MeshRenderer>()->mesh->triangles.empty(), "Scrolled button did not become visible");
    p = pointer(100, 104, 200, 200); p.set(MouseButton::Left, true); ui.update(scene, p); p.beginFrame(); p.set(MouseButton::Left, false); ui.update(scene, p);
    check(clicked == 1, "Visible scrolled button did not receive a click");
    p = pointer(100, 35, 200, 200); p.set(MouseButton::Left, true); ui.update(scene, p);
    p.beginFrame(); p.set(MouseButton::Left, false); p.text = "new"; ui.update(scene, p);
    check(input.text == "new", "Input focus/text replacement failed");
    p.beginFrame(); p.set(Key::Enter, true); ui.update(scene, p); check(committed == "new" && !ui.editingText(), "Input commit failed");
    ui.focus(field); p = pointer(100, 35, 200, 200); p.text = "cancel"; ui.update(scene, p);
    p.beginFrame(); p.set(Key::Escape, true); ui.update(scene, p); check(input.text == "new", "Escape did not restore input text");
    ui.cancel();
}

void editorChecks(const fs::path& scratch) {
    Scene initial; auto& cubeObject = initial.entities.emplace_back(); cubeObject.name = "Seed";
    cubeObject.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()));
    std::ofstream(scratch / "engine.json") << "{\"project\":\"\",\"mode\":\"editor\",\"autoStart\":false}";
    editor::Editor editor(std::move(initial), scratch, {}, scratch / "engine.json");
    check(!editor.executeScripts(), "Editor executes scene scripts");
    editor.newProject("TestGame"); check(fs::is_directory(scratch / "Projects/TestGame/Assets"), "Project Assets missing");
    auto manifest = scratch / "Projects/TestGame/project.json"; check(fs::is_regular_file(manifest), "Project manifest missing");
    std::ifstream config(scratch / "engine.json"); auto settings = json::parse(std::string((std::istreambuf_iterator<char>(config)), {}));
    config.close();
    check(settings.at("project").string() == "Projects/TestGame" && settings.at("mode").string() == "editor", "Project selection was not remembered");
    rejects([&] { editor.newProject("../escape"); }); rejects([&] { editor.newProject("TestGame"); });
    auto index = editor.addObject("Cube"); editor.addComponent(index, "BoxCollider");
    editor.change([&](Scene& scene) { scene.entities[index].transform.localPosition = {3, 2, 1}; });
    check(editor.dirty(), "Edits did not dirty scene"); editor.undo(); check(close(editor.content().entities[index].transform.localPosition, {}), "Transform undo failed");
    editor.redo(); check(close(editor.content().entities[index].transform.localPosition, {3, 2, 1}), "Transform redo failed");
    editor.save(); check(!editor.dirty(), "Save did not clear dirty state");
    auto saved = editor.scenePath(); editor.newScene("Second"); check(editor.content().entities.size() == 1, "New scene did not reset content");
    editor.openScene(saved); check(editor.content().entities.size() == 2, "Scene reload lost objects");
    check(editor.content().entities[1].getComponent<BoxCollider>(), "Reload lost component");
    editor.addObject(); editor.undo(); editor.addObject("Plane"); editor.redo(); check(editor.content().entities.back().name == "Plane", "New edit did not clear redo");
    editor.openProject(scratch / "Projects/TestGame"); check(editor.content().entities.size() == 2, "Open project did not load startup scene");
    editor.update(0, pointer(0, 0));
    clickButton(editor, "File"); check(findButton(editor.scene(), "New Scene"), "File dropdown missing");
    clickButton(editor, "New Scene"); check(findButton(editor.scene(), "Create"), "New Scene dialog missing");
    for (auto& e : editor.scene().entities) if (auto* f = e.getComponent<InputField>(); f && e.visibleInHierarchy()) f->text = "FromUI";
    clickButton(editor, "Create"); check(editor.scenePath().filename() == "FromUI.scene.json", "New Scene UI did not create file");
    clickButton(editor, "+"); clickButton(editor, "Cube"); check(editor.content().entities.back().name == "Cube", "Hierarchy creation UI failed");
    auto inspectorScroll = pointer(1120, 320); inspectorScroll.mouseWheel = -30; editor.update(.016f, inspectorScroll);
    clickButton(editor, "+ Add Component"); clickButton(editor, "BoxCollider");
    check(editor.content().entities.back().getComponent<BoxCollider>(), "Inspector Add Component UI failed");
    inspectorScroll.mouseWheel = 30; editor.update(.016f, inspectorScroll);
    Entity* nameField = nullptr;
    for (auto& e : editor.scene().entities) if (auto* f = e.getComponent<InputField>(); e.layer == 31 && f && f->text == "Cube") { nameField = &e; break; }
    check(nameField, "Inspector name field missing");
    auto namePosition = nameField->transform.position();
    click(editor, int(640 + namePosition.x), int(400 - namePosition.y));
    auto typing = pointer(int(640 + namePosition.x), int(400 - namePosition.y)); typing.text = "Renamed"; editor.update(.016f, typing);
    // Clicking another control must commit the field and retain that control's click.
    clickButton(editor, "File"); check(editor.content().entities.back().name == "Renamed", "Inspector field blur did not commit");
    clickButton(editor, "New Scene"); check(findButton(editor.scene(), "Discard"), "Unsaved switch confirmation missing");
    clickButton(editor, "Cancel"); check(editor.scenePath().filename() == "FromUI.scene.json", "Cancel switched scenes");
    clickButton(editor, "Edit"); clickButton(editor, "Save CTRL+S"); check(!editor.dirty(), "Save menu UI failed");
    editor.update(0, pointer(0, 0));
    // Double-click in the Project browser must actually load the selected JSON scene.
    clickButton(editor, "Main.scene.json"); clickButton(editor, "Main.scene.json"); check(editor.scenePath().filename() == "Main.scene.json", "Project double-click failed");
    // Center the selected object and drag the red Move handle; one drag is one undo step.
    clickButton(editor, "0: Seed"); auto p = pointer(0, 0); p.set(Key::F, true); editor.update(.016f, p);
    Entity* handle = nullptr;
    for (auto& e : editor.scene().entities) if (e.layer == 30) { handle = &e; break; }
    check(handle, "Transform handles missing");
    const auto* camera = [&]() -> const Entity* { for (auto& e : editor.scene().entities) if (auto* c = e.getComponent<Camera>(); c && c->enabled && c->layers == layerMask(0)) return &e; return nullptr; }();
    check(camera, "Editor camera missing");
    const float size = std::max(.3f, length(handle->transform.position() - camera->transform.position()) * .09f);
    auto v = inverseRotate(handle->transform.position() + Vector3{size, 0, 0} - camera->transform.position(), camera->transform.rotation());
    int x = int((v.x / (v.z * std::tan(pi / 6) * 1280 / 800) + 1) * 640);
    int y = int((1 - v.y / (v.z * std::tan(pi / 6))) * 400);
    p = pointer(x, y); p.set(MouseButton::Left, true); editor.update(.016f, p);
    p.beginFrame(); p.mouseX += 45; editor.update(.016f, p);
    p.beginFrame(); p.set(MouseButton::Left, false); editor.update(.016f, p);
    check(editor.content().entities[0].transform.localPosition.x > .1f, "Move gizmo did not transform object");
    editor.undo(); check(close(editor.content().entities[0].transform.localPosition, {}), "Gizmo drag undo failed");
    editor.redo(); check(editor.content().entities[0].transform.localPosition.x > .1f, "Gizmo drag redo failed");
    editor.undo();
    const auto dragFirstHandle = [&](const std::string& tool) {
        clickButton(editor, tool);
        Entity* first = nullptr; const Entity* camera = nullptr;
        for (auto& e : editor.scene().entities) {
            if (!first && e.layer == 30) first = &e;
            if (auto* c = e.getComponent<Camera>(); c && c->enabled && c->layers == layerMask(0)) camera = &e;
        }
        check(first && camera, "Tool handle missing");
        auto mesh = first->getComponent<MeshRenderer>()->mesh;
        Vector3 local{size, 0, 0};
        if (tool.starts_with("Rotate")) { local = {}; for (int i = 0; i < 8; ++i) local = local + mesh->vertices[i] * .125f; }
        auto v = inverseRotate(first->transform.point(local) - camera->transform.position(), camera->transform.rotation());
        int x = int((v.x / (v.z * std::tan(pi / 6) * 1280 / 800) + 1) * 640);
        int y = int((1 - v.y / (v.z * std::tan(pi / 6))) * 400);
        auto p = pointer(x, y); p.set(MouseButton::Left, true); editor.update(.016f, p);
        p.beginFrame(); p.mouseX += 40; p.mouseY -= 35; editor.update(.016f, p);
        p.beginFrame(); p.set(MouseButton::Left, false); editor.update(.016f, p);
    };
    dragFirstHandle("Rotate E");
    check(length(editor.content().entities[0].transform.localRotation.toEuler()) > .01f, "Rotate gizmo did not rotate object");
    editor.undo(); check(length(editor.content().entities[0].transform.localRotation.toEuler()) < .001f, "Rotate gizmo undo failed");
    dragFirstHandle("Scale R");
    check(!close(editor.content().entities[0].transform.localScale, {1, 1, 1}), "Scale gizmo did not scale object");
    editor.undo(); check(close(editor.content().entities[0].transform.localScale, {1, 1, 1}), "Scale gizmo undo failed");
    editor.newProject("CopyGame"); check(fs::is_regular_file(scratch / "Projects/CopyGame/Assets/Second.scene.json"), "New project did not copy assets");
    check(editor.scenePath().filename() == "Main2.scene.json", "Project copy overwrote an existing scene");
    Renderer renderer(1280, 800); editor.update(0, pointer(0, 0)); renderer.render(editor.scene()); renderer.savePPM((scratch / "editor.ppm").string());
}

int main() {
    try {
        sceneChecks(); uiChecks();
        const auto scratch = fs::current_path() / ("editor-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(scratch); editorChecks(scratch);
        const auto runtime = fs::current_path() / "scene-project-check"; fs::create_directories(runtime / "Assets");
        Scene gameScene; auto& camera = gameScene.entities.emplace_back(); camera.name = "Camera";
        camera.transform.localPosition = {0, 0, -5}; camera.addComponent<Camera>();
        auto& object = gameScene.entities.emplace_back(); object.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()));
        object.addComponent<EryScript>("using tiny3d\nmethod Awake\n entity.transform.localPosition.x = 0.5\nmend\n");
        saveScene(gameScene, runtime / "Assets/Main.scene.json");
        std::ofstream manifest(runtime / "project.json"); manifest << "{\"sources\":[],\"startupScene\":\"Assets/Main.scene.json\",\"width\":640,\"height\":400,\"fullscreen\":false}\n";
        std::cout << "Scene, UI, project, editor menus, and gizmo checks passed. Artifacts: " << scratch.string() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
