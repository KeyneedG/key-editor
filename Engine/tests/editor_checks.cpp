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

void applicationChecks() {
    check(Application::targetFramerate == 0 && !Application::vSync, "Application should start uncapped with VSync disabled");
    Scene scene;
    auto& script = scene.entities.emplace_back().addComponent<EryScript>(
        "using tiny3d\nmethod Awake\n Application.targetFramerate = 144\n Application.vSync = true\nmend\n"
        "method Update\n Application.targetFramerate = 0\n Application.vSync = false\nmend\n");
    Scripts scripts; scripts.awake(scene);
    check(script.error().empty(), "Application settings are unavailable to EryScript");
    check(Application::targetFramerate == 144 && Application::vSync, "Script did not update static Application settings");
    scripts.update(scene, .01f, {});
    check(script.error().empty() && Application::targetFramerate == 0 && !Application::vSync,
        "Script did not disable FPS limiting and VSync at runtime");
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

void projectStartupChecks(const fs::path& scratch) {
    const auto source = scratch / "FirstRunProject"; fs::create_directories(source);
    std::ofstream(source / "project.json") << "{\"sources\":[]}";
    std::ofstream(source / "RootOnly.txt") << "Project metadata, not an asset";
    const auto expectAssets = [&](editor::Editor& editor) {
        bool heading = false;
        for (auto& e : editor.scene().entities) if (auto* text = e.getComponent<SimpleText>(); text && e.layer == 31 && text->text == "Assets") heading = true;
        check(heading && !findButton(editor.scene(), "RootOnly.txt") && !findButton(editor.scene(), "project.json") &&
            !findButton(editor.scene(), ".. Parent folder"), "Project browser did not start inside Assets");
    };
    editor::Editor firstRun({}, scratch, source);
    check(fs::is_directory(source / "Assets"), "First run did not create the missing Assets directory"); expectAssets(firstRun);
    firstRun.newProject("StartupGame"); firstRun.update(0, pointer(0, 0)); expectAssets(firstRun);
    check(findButton(firstRun.scene(), "Main.scene.json"), "New project assets were not visible");
    const auto target = firstRun.projectDirectory(); fs::create_directories(target / "Assets/Textures");
    std::ofstream(target / "Assets/Textures/Texture.txt") << "Asset";
    clickButton(firstRun, "Refresh"); clickButton(firstRun, "[DIR] Textures"); clickButton(firstRun, "[DIR] Textures");
    check(findButton(firstRun.scene(), "Texture.txt") && findButton(firstRun.scene(), ".. Parent folder"), "Asset subfolder navigation failed");
    editor::Editor restarted({}, scratch, target); expectAssets(restarted);
    check(findButton(restarted.scene(), "Main.scene.json"), "Restart did not show the new project's asset contents");
    firstRun.openProject(target); firstRun.update(0, pointer(0, 0)); expectAssets(firstRun);
}

void cameraChecks(const fs::path& scratch) {
    editor::Editor editor({}, scratch);
    const auto camera = [&]() -> Transform {
        for (auto& e : editor.scene().entities)
            if (auto* c = e.getComponent<Camera>(); c && c->enabled && c->layers == layerMask(0)) return e.transform;
        throw std::runtime_error("Editor camera missing");
    };
    float yaw = 0, pitch = .18f;
    const auto expectRotation = [&] {
        const auto expected = Quaternion::fromEuler({pitch, yaw, 0}), actual = camera().rotation();
        for (Vector3 axis : {Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, 0, 1}})
            check(close(rotate(axis, actual), rotate(axis, expected)), "Editor camera changed axis, clamped, or jumped at an angle boundary");
    };
    const auto turn = [&](Input& input, float dx, float dy) {
        input.beginFrame(); input.mouseDeltaX = dx; input.mouseDeltaY = dy;
        yaw = std::remainder(yaw + dx * .003f, 2 * pi); pitch = std::remainder(pitch + dy * .003f, 2 * pi);
        editor.update(.016f, input); expectRotation();
    };
    auto input = pointer(500, 300); input.set(MouseButton::Right, true);
    input.mouseDeltaX = 500; input.mouseDeltaY = 700; editor.update(.016f, input);
    check(editor.captureMouse(), "Right-button look did not capture the mouse"); expectRotation();
    for (int i = 0; i < 45; ++i) turn(input, 31, 0); // Through 90, 180, and the yaw wrap.
    for (int i = 0; i < 36; ++i) turn(input, 0, 80); // Through both pitch poles and a complete turn.
    input.mouseX = 80; input.mouseY = 180; turn(input, 20, 30);
    check(editor.captureMouse(), "Camera look stopped when the cursor left the viewport");
    for (int i = 0; i < 15; ++i) turn(input, 40, -35);
    for (int i = 0; i < 15; ++i) turn(input, -40, 35);
    for (int i = 0; i < 5; ++i) turn(input, 0, 0);
    const auto beforeFlight = camera().position();
    input.beginFrame(); input.set(Key::W, true); editor.update(.1f, input);
    check(close(camera().position(), beforeFlight + camera().forward() * .6f), "Camera fly movement did not follow its current orientation");
    input.beginFrame(); input.set(Key::W, false); editor.update(.016f, input);
    input.beginFrame(); input.set(MouseButton::Right, false); editor.update(.016f, input);
    check(!editor.captureMouse(), "Releasing right-button look kept the mouse captured"); expectRotation();
    input = pointer(500, 300); input.set(MouseButton::Right, true); input.set(MouseButton::Right, false); editor.update(.016f, input);
    check(!editor.captureMouse(), "A completed right click started mouse capture"); expectRotation();

    input = pointer(100, 200); input.set(MouseButton::Right, true); editor.update(.016f, input);
    check(!editor.captureMouse(), "Right-click in the Hierarchy started camera look");
    input.beginFrame(); input.mouseX = 500; input.mouseY = 300; input.mouseDeltaX = 90; editor.update(.016f, input);
    check(!editor.captureMouse(), "Dragging into the viewport unexpectedly started camera look"); expectRotation();
    input.beginFrame(); input.set(MouseButton::Right, false); editor.update(.016f, input);
    input.beginFrame(); input.set(MouseButton::Right, true); editor.update(.016f, input);
    input.beginFrame(); input.focused = false; input.mouseDeltaY = 200; editor.update(.016f, input);
    check(!editor.captureMouse(), "Focus loss kept the editor mouse captured"); expectRotation();
    input.beginFrame(); input.focused = true; input.mouseDeltaX = 100; editor.update(.016f, input);
    check(!editor.captureMouse(), "Regaining focus resumed look without a new right click"); expectRotation();
    input.beginFrame(); input.set(MouseButton::Right, false); editor.update(.016f, input);
    input.beginFrame(); input.set(MouseButton::Right, true); editor.update(.016f, input);
    input.beginFrame(); input.set(Key::Escape, true); editor.update(.016f, input);
    check(!editor.captureMouse(), "Escape did not release camera capture"); expectRotation();
    input.beginFrame(); input.mouseDeltaY = 100; editor.update(.016f, input); expectRotation();
    check(!editor.dirty(), "Editor camera movement dirtied the scene");
}

void cameraNavigationChecks(const fs::path& scratch) {
    Scene initial;
    auto& target = initial.entities.emplace_back(); target.name = "Target";
    target.transform.localPosition = {2, 1, 0};
    target.transform.localRotation = Quaternion::fromEuler({.2f, .4f, .1f});
    target.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()));
    editor::Editor editor(std::move(initial), scratch);
    clickButton(editor, "0: Target"); clickButton(editor, "World");
    const auto content = serializeScene(editor.content());
    const auto camera = [&](unsigned layer) -> const Entity& {
        for (auto& e : editor.scene().entities)
            if (auto* c = e.getComponent<Camera>(); c && c->enabled && c->layers == layerMask(layer)) return e;
        throw std::runtime_error("Navigation camera missing");
    };
    for (const auto* tool : {"Move W", "Rotate E", "Scale R"}) {
        clickButton(editor, tool);
        struct Visual { std::size_t entity, component; std::shared_ptr<const Mesh> mesh; };
        std::vector<Visual> cached;
        const auto count = editor.scene().entities.size();
        for (std::size_t i = 0; i < count; ++i) {
            const auto& e = editor.scene().entities[i];
            for (std::size_t c = 0; c < e.components.size(); ++c)
                if (auto* visual = dynamic_cast<const MeshRenderer*>(e.components[c].get())) cached.push_back({i, c, visual->mesh});
        }
        const auto verify = [&] {
            check(editor.scene().entities.size() == count, "Camera navigation recreated editor entities");
            for (const auto& v : cached) {
                const auto* visual = dynamic_cast<const MeshRenderer*>(editor.scene().entities[v.entity].components.at(v.component).get());
                check(visual && visual->mesh == v.mesh, "Camera navigation recreated scene, UI, or gizmo meshes");
            }
            const auto& world = camera(0).transform;
            const auto& gizmo = camera(30).transform;
            check(close(world.position(), gizmo.position()) && close(world.forward(), gizmo.forward()) && close(world.up(), gizmo.up()),
                "Gizmo camera did not follow viewport navigation");
            const auto& selected = editor.content().entities[0].transform;
            const float size = std::max(.3f, length(selected.position() - world.position()) * .09f);
            unsigned handles = 0;
            for (const auto& e : editor.scene().entities) if (e.layer == 30) {
                ++handles;
                check(close(e.transform.position(), selected.position()) && close(e.transform.localScale, {size, size, size}),
                    "Cached gizmo size or position did not follow camera distance");
                check(close(e.transform.right(), selected.right()) && close(e.transform.up(), selected.up()),
                    "Camera navigation changed local gizmo orientation");
            }
            check(handles == 3, "Navigation lost transform handles");
        };
        auto input = pointer(500, 300); input.set(MouseButton::Right, true); editor.update(.016f, input); verify();
        for (int frame = 0; frame < 6; ++frame) {
            input.beginFrame(); input.mouseDeltaX = 12; input.mouseDeltaY = -7;
            input.set(Key::W, true); input.set(Key::Shift, frame >= 3);
            editor.update(.016f, input); verify();
        }
        editor.update(.016f, pointer(500, 300)); verify();
        const auto beforeWheel = camera(0).transform.position();
        auto wheel = pointer(500, 300); wheel.mouseWheel = 2; editor.update(.016f, wheel); verify();
        check(close(camera(0).transform.position(), beforeWheel + camera(0).transform.forward() * 2), "Wheel navigation did not move camera");
        auto focus = pointer(0, 0); focus.set(Key::F, true); editor.update(.016f, focus); verify();
        check(close(camera(0).transform.position(), editor.content().entities[0].transform.position() - camera(0).transform.forward() * 4),
            "Framing selection did not move the cached cameras");
        wheel.mouseWheel = 3; editor.update(.016f, wheel); verify(); // Exercise the minimum handle size.
    }
    check(!editor.dirty() && serializeScene(editor.content()) == content, "Navigation changed the editable scene");
}

void transformSpaceChecks(const fs::path& scratch) {
    Scene scene; auto& parent = scene.entities.emplace_back(); parent.name = "Parent";
    parent.transform.localRotation = Quaternion::fromEuler({.2f, .6f, .35f}); parent.transform.localScale = {1.4f, .8f, 1.2f};
    auto& child = scene.entities.emplace_back(); child.name = "Child"; child.setParent(&parent, false);
    child.transform.localPosition = {.5f, .6f, 0}; child.transform.localRotation = Quaternion::fromEuler({.15f, .25f, -.2f});
    child.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()));
    editor::Editor editor(std::move(scene), scratch);
    clickButton(editor, "1: Child"); auto focus = pointer(0, 0); focus.set(Key::F, true); editor.update(.016f, focus);
    const auto initial = serializeScene(editor.content());
    const auto firstHandle = [&]() -> Entity& {
        for (auto& e : editor.scene().entities) if (e.layer == 30) return e;
        throw std::runtime_error("Space gizmo missing");
    };
    const auto drag = [&](const std::string& tool, bool cancel = false) {
        clickButton(editor, tool);
        auto& handle = firstHandle(); auto mesh = handle.getComponent<MeshRenderer>()->mesh;
        // Pick away from the rotation rings' intersections.
        Vector3 local{}; const auto first = tool.starts_with("Rotate") ? 6 * 8 : mesh->vertices.size() - 8;
        for (std::size_t i = first; i < first + 8; ++i) local = local + mesh->vertices[i] * .125f;
        const Entity* camera = nullptr;
        for (auto& e : editor.scene().entities) if (auto* c = e.getComponent<Camera>(); c && c->enabled && c->layers == layerMask(0)) camera = &e;
        check(camera, "Space test camera missing");
        const auto project = [&](Vector3 point) {
            const auto v = inverseRotate(point - camera->transform.position(), camera->transform.rotation());
            return Vector3{(v.x / (v.z * std::tan(pi / 6) * 1280 / 800) + 1) * 640, (1 - v.y / (v.z * std::tan(pi / 6))) * 400, 0};
        };
        const auto worldPoint = handle.transform.point(local), screen = project(worldPoint);
        const auto axis = handle.transform.right();
        const auto direction = tool.starts_with("Rotate") ? normalized(cross(axis, worldPoint - handle.transform.position())) : axis;
        const auto screenAxis = normalized(project(worldPoint + direction) - screen);
        auto p = pointer(int(std::lround(screen.x)), int(std::lround(screen.y))); p.set(MouseButton::Left, true); editor.update(.016f, p);
        p.beginFrame(); p.mouseX += int(std::lround(screenAxis.x * 45)); p.mouseY += int(std::lround(screenAxis.y * 45)); editor.update(.016f, p);
        p.beginFrame(); if (cancel) p.set(Key::Escape, true); else p.set(MouseButton::Left, false); editor.update(.016f, p);
    };
    check(close(firstHandle().transform.right(), {1, 0, 0}), "World gizmo is not aligned to world axes");
    clickButton(editor, "World");
    check(findButton(editor.scene(), "Local") && !editor.dirty(), "Space toggle dirtied the scene or did not update its label");
    const auto originalRotation = editor.content().entities[1].transform.rotation();
    const auto localAxis = rotate({1, 0, 0}, originalRotation);
    check(close(firstHandle().transform.right(), localAxis), "Local gizmo ignores object/parent rotation");
    const auto beforePosition = editor.content().entities[1].transform.position();
    drag("Move W");
    auto movement = editor.content().entities[1].transform.position() - beforePosition;
    check(length(movement) > .1f && length(cross(normalized(movement), localAxis)) < .001f, "Local movement used the wrong axis");
    editor.undo(); editor.update(0, pointer(0, 0)); check(serializeScene(editor.content()) == initial, "Local movement undo failed");
    drag("Rotate E");
    auto delta = normalized(editor.content().entities[1].transform.rotation() * inverse(originalRotation));
    check(length(Vector3{delta.x, delta.y, delta.z}) > .01f && length(cross(normalized({delta.x, delta.y, delta.z}), localAxis)) < .001f, "Local rotation used the wrong axis");
    editor.undo(); editor.update(0, pointer(0, 0));
    drag("Scale R");
    auto scale = editor.content().entities[1].transform.localScale;
    check(std::abs(scale.x - 1) > .1f && scale.y == 1 && scale.z == 1, "Local scale changed unrelated axes");
    editor.undo(); editor.update(0, pointer(0, 0));
    clickButton(editor, "Local");
    drag("Move W"); movement = editor.content().entities[1].transform.position() - beforePosition;
    check(movement.x > .1f && std::abs(movement.y) < .001f && std::abs(movement.z) < .001f, "World movement followed parent axes");
    editor.undo(); editor.update(0, pointer(0, 0));
    drag("Rotate E"); delta = normalized(editor.content().entities[1].transform.rotation() * inverse(originalRotation));
    check(std::abs(delta.x) > .01f && std::abs(delta.y) < .001f && std::abs(delta.z) < .001f, "World rotation used local axes");
    editor.undo(); editor.update(0, pointer(0, 0));
    drag("Scale R"); scale = editor.content().entities[1].transform.localScale;
    check(scale.x > 1 && scale.y > 1 && scale.z > 1, "World scale did not project onto rotated local axes");
    editor.undo(); editor.update(0, pointer(0, 0));
    drag("Move W", true); check(serializeScene(editor.content()) == initial, "Cancelling a world-space drag changed the scene");
    check(!editor.dirty(), "Space controls or cancelled drag left the scene dirty");
}

void statsChecks(editor::Editor& editor) {
    const auto findStats = [&]() -> SimpleText* {
        for (auto& e : editor.scene().entities) if (auto* text = e.getComponent<SimpleText>(); text && text->text.starts_with("Test backend")) return text;
        return nullptr;
    };
    check(!findStats(), "Stats panel should start hidden");
    clickButton(editor, "0: Seed");
    graphics::FrameStats frame{{17, 204, 136, 3, 2}, "Test backend", 1280, 800, .5f, .0012f, .0023f, .0034f};
    frame.rendering.meshSubmissions = 21; frame.rendering.instances = 37;
    frame.rendering.instancedBatches = 2; frame.rendering.glyphInstances = 30;
    editor.frameRendered(frame); clickButton(editor, "Stats");
    auto* text = findStats(); check(text, "Stats panel did not open");
    check(text->text.find("FPS: 2.0 | Frame: 500.0 ms") != std::string::npos, "Stats frame time was clamped to simulation time");
    check(text->text.find("Batches / draws: 17") != std::string::npos && text->text.find("Triangles: 204") != std::string::npos, "Stats omitted backend counts");
    check(text->text.find("Instances: 37 | Glyphs: 30") != std::string::npos && text->text.find("Instanced: 2 | Saved draws: 4") != std::string::npos,
        "Stats omitted instance counts or draw savings");
    const auto count = editor.scene().entities.size();
    frame.frameSeconds = .25f; frame.rendering.batches = 23; editor.frameRendered(frame); editor.update(.016f, pointer(0, 0));
    check(findStats() == text && editor.scene().entities.size() == count && text->text.find("Batches / draws: 23") != std::string::npos, "Live stats rebuilt the editor or did not update");
    const auto before = serializeScene(editor.content());
    click(editor, 800, 200); check(serializeScene(editor.content()) == before, "Stats panel click edited scene content");
    unsigned handles = 0; for (auto& e : editor.scene().entities) if (e.layer == 30) ++handles;
    check(handles == 3, "Stats panel click changed object selection");
    clickButton(editor, "Stats"); check(!findStats(), "Stats panel did not hide");
}

void editorChecks(const fs::path& scratch) {
    Scene initial; auto& cubeObject = initial.entities.emplace_back(); cubeObject.name = "Seed";
    cubeObject.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()));
    std::ofstream(scratch / "engine.json") << "{\"project\":\"\",\"mode\":\"editor\",\"autoStart\":false}";
    editor::Editor editor(std::move(initial), scratch, {}, scratch / "engine.json");
    check(!editor.executeScripts(), "Editor executes scene scripts");
    statsChecks(editor);
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
        Vector3 local{1, 0, 0};
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
    Renderer renderer(1280, 800); editor.update(0, pointer(0, 0)); renderer.render(editor.scene());
    editor.frameRendered({renderer.stats(), "Software", 1280, 800, .25f, .001f, .002f, 0}); clickButton(editor, "Stats");
    renderer.render(editor.scene()); renderer.savePPM((scratch / "editor.ppm").string());
}

int main() {
    try {
        applicationChecks(); sceneChecks(); uiChecks();
        const auto scratch = fs::current_path() / ("editor-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(scratch); projectStartupChecks(scratch); cameraChecks(scratch); cameraNavigationChecks(scratch); transformSpaceChecks(scratch); editorChecks(scratch);
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
