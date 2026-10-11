#include "tiny3d/behaviour.hpp"
#include "tiny3d/editor.hpp"
#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace tiny3d;
namespace fs = std::filesystem;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
template<class F> void rejects(F action) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid operation was accepted");
}
std::vector<std::string> events;
class Probe final : public MonoBehaviour {
public:
    float speed = 2;
    int awakes = 0, starts = 0, updates = 0, lates = 0;
    std::function<void()> action;
    void Awake() override {
        ++awakes; events.push_back("Awake");
        check(&transform() == &gameObject().transform && &name() == &gameObject().name &&
            &tag() == &gameObject().tag && &layer() == &gameObject().layer, "Owner accessors are detached copies");
        check(!scene().entities.empty(), "Awake has no scene");
    }
    void Start() override { ++starts; events.push_back("Start"); }
    void Update(float seconds, const Input& input) override {
        ++updates; events.push_back("Update"); transform().localPosition.x += speed * seconds;
        if (input.pressed(Key::A)) tag() = "Input received";
        if (action) { auto callback = action; callback(); }
    }
    void LateUpdate(float, const Input&) override { ++lates; events.push_back("LateUpdate"); }
    json::Value saveFields() const override { return json::Value::Object{{"speed", speed}}; }
    void loadFields(const json::Value& fields) override {
        if (fields.has("speed")) speed = static_cast<float>(fields.at("speed").number());
    }
};
class Unregistered final : public MonoBehaviour {};
class Failing final : public MonoBehaviour {
public:
    void Awake() override { throw std::runtime_error("Expected callback failure"); }
};
TINY3D_REGISTER_BEHAVIOUR(Failing)
static_assert(std::same_as<decltype(std::declval<const Probe&>().transform()), const Transform&>);
static_assert(std::same_as<decltype(std::declval<const Probe&>().gameObject()), const Entity&>);

void lifecycleChecks() {
    Probe detached; rejects([&] { detached.gameObject(); }); rejects([&] { detached.scene(); });
    Entity original;
    auto& probe = original.addComponent<Probe>(); probe.name() = "Native object"; probe.layer() = 3;
    auto& camera = probe.addComponent<Camera>(); check(probe.getComponent<Camera>() == &camera, "Sibling component helpers failed");
    const Probe& constant = probe; check(constant.getComponent<Camera>() == &camera, "Const component helper failed");
    Scene scene; scene.entities.push_back(std::move(original));
    check(&probe.gameObject() == &scene.entities.front(), "Moving an entity did not rebind its components");
    auto& second = scene.entities.emplace_back().addComponent<Probe>();
    second.enabled = false; events.clear(); Scripts scripts; scripts.awake(scene); scripts.awake(scene);
    check(probe.awakes == 1 && !probe.starts && !second.awakes, "Awake was repeated or ran on a disabled behaviour");
    second.enabled = true; events.clear(); Input input; input.set(Key::A, true); scripts.update(scene, .25f, input);
    check(events == std::vector<std::string>{"Awake", "Start", "Start", "Update", "Update", "LateUpdate", "LateUpdate"},
        "Native lifecycle ordering is wrong");
    check(probe.tag() == "Input received" && probe.transform().localPosition.x == .5f, "Update did not receive time/input or change its owner");
    check(&probe.gameObject() != &second.gameObject(), "Script instances share an owner");
    probe.enabled = false; scripts.update(scene, .1f, {}); check(probe.updates == 1, "Disabled behaviour updated");
    probe.enabled = true; scripts.update(scene, .1f, {});
    check(probe.awakes == 1 && probe.starts == 1 && probe.updates == 2, "Re-enabling restarted the lifecycle");
    Entity destination; destination = std::move(scene.entities.front());
    check(&probe.gameObject() == &destination && &camera.gameObject() == &destination, "Move assignment lost component owners");
    rejects([&] { scripts.update(scene, -1, {}); });
    rejects([&] { scripts.update(scene, std::numeric_limits<float>::infinity(), {}); });
}
void mutationChecks() {
    Scene scene; auto& owner = scene.entities.emplace_back(); auto& first = owner.addComponent<Probe>();
    auto& removed = scene.entities.emplace_back().addComponent<Probe>(); auto* removedPointer = &removed;
    first.action = [&] {
        auto& components = scene.entities[1].components;
        std::erase_if(components, [&](const auto& c) { return c.get() == removedPointer; });
        scene.entities.emplace_back().addComponent<Probe>(); first.action = {};
    };
    Scripts scripts; scripts.update(scene, .1f, {});
    auto* added = scene.entities.back().getComponent<Probe>();
    check(added && !added->awakes && !added->updates, "A new behaviour ran during its creation tick");
    scripts.update(scene, .1f, {}); check(added->awakes == 1 && added->starts == 1 && added->updates == 1, "A new behaviour did not start next tick");
    auto& failing = scene.entities.emplace_back().addComponent<Failing>();
    scripts.update(scene, .1f, {}); check(!failing.error().empty() && first.updates == 3, "Callback failure stopped other scripts");
    // Removing the currently executing component must not dereference the old snapshot afterwards.
    first.action = [&] { owner.components.clear(); };
    scripts.update(scene, .1f, {}); check(owner.components.empty(), "Self removal failed");
}
void persistenceChecks() {
    Scene scene; auto& object = scene.entities.emplace_back();
    auto& probe = dynamic_cast<Probe&>(addBehaviour(object, "Probe")); probe.speed = 7; probe.enabled = false;
    auto encoded = serializeScene(scene); auto loaded = deserializeScene(encoded);
    const auto* restored = loaded.entities.front().getComponent<Probe>();
    check(restored && restored->speed == 7 && !restored->enabled && !restored->awakes &&
        &restored->gameObject() == &loaded.entities.front(), "Native script type/fields/enabled/owner did not round-trip");
    auto document = json::parse(encoded);
    auto& component = std::get<json::Value::Array>(std::get<json::Value::Array>(document["entities"].data)[0]["components"].data)[0];
    component["script"] = "NotCompiled";
    auto missing = deserializeScene(json::stringify(document));
    auto* placeholder = missing.entities.front().getComponent<MonoBehaviour>();
    check(placeholder && missingBehaviour(*placeholder), "Missing script did not retain a placeholder");
    check(serializeScene(missing) == json::stringify(document), "Missing script fields/identity were discarded");
    placeholder->enabled = true; Scripts scripts; scripts.awake(missing);
    check(placeholder->error().find("Rebuild") != std::string::npos, "Missing runtime script did not report a rebuild diagnostic");
    object.addComponent<Unregistered>(); rejects([&] { serializeScene(scene); });
    rejects([&] { addBehaviour(object, "Unknown"); });
    rejects([&] { restoreBehaviour("Probe", json::Value::Array{}); });
}
Input pointer() { Input i; i.focused = true; i.renderWidth = 1280; i.renderHeight = 800; i.viewport = {0, 0, 1280, 800}; return i; }
Entity* findButton(Scene& scene, const std::string& name) {
    for (auto& e : scene.entities) if (e.getComponent<Button>()) for (std::size_t i = 0; i < e.childCount(); ++i)
        if (auto* text = e.getChild(i)->getComponent<SimpleText>(); text && text->text == name) return &e;
    return nullptr;
}
void click(editor::Editor& editor, const std::string& name) {
    auto* button = findButton(editor.scene(), name); check(button, "Missing editor control"); auto position = button->transform.position();
    auto input = pointer(); input.mouseX = int(640 + position.x); input.mouseY = int(400 - position.y);
    input.set(MouseButton::Left, true); editor.update(.016f, input);
    input.beginFrame(); input.set(MouseButton::Left, false); editor.update(.016f, input);
}
void editorChecks(const fs::path& scratch) {
    fs::create_directories(scratch / "Assets/Scripts"); fs::create_directories(scratch / "build");
    std::ofstream(scratch / "project.json") << "{\"sources\":[\"Legacy.cpp\"]}";
    std::ofstream(scratch / "Assets/Root.cpp");
    fs::create_directories(scratch / "Assets/Scripts/Nested"); std::ofstream(scratch / "Assets/Scripts/Nested/Deep.cpp");
    std::ofstream(scratch / "Legacy.cpp"); std::ofstream(scratch / "Assets/Scripts/Probe.cpp");
    std::ofstream(scratch / "Assets/Scripts/Probe.HPP"); std::ofstream(scratch / "build/Generated.cpp");
    registerBehaviour<Probe>("Probe", scratch / "Assets/Scripts/Probe.cpp");
    editor::Editor editor({}, scratch / "Engine", scratch);
    check(findButton(editor.scene(), "[C++] Root.cpp") && findButton(editor.scene(), "[DIR] Scripts") &&
        !findButton(editor.scene(), "[C++] Legacy.cpp") && !findButton(editor.scene(), "[C++] Assets/Scripts/Probe.cpp"),
        "Assets root flattened scripts from another folder");
    click(editor, "[DIR] Scripts"); click(editor, "[DIR] Scripts");
    check(findButton(editor.scene(), "[C++] Probe.cpp") && findButton(editor.scene(), "[C++] Probe.HPP") &&
        findButton(editor.scene(), "[DIR] Nested") && !findButton(editor.scene(), "[C++] Nested/Deep.cpp"),
        "Assets subfolder did not list its own scripts/headers or flattened a deeper folder");
    const auto object = editor.addObject(); editor.update(0, pointer());
    click(editor, "[C++] Probe.cpp"); click(editor, "[C++] Probe.cpp");
    check(editor.content().entities[object].getComponent<Probe>(), "Double-clicking a native script did not attach it");
    editor.undo(); check(!editor.content().entities[object].getComponent<Probe>(), "Undo did not remove the native script");
    editor.redo(); check(editor.content().entities[object].getComponent<Probe>(), "Redo did not restore the native script");
    editor.update(0, pointer()); click(editor, "+ Add Component"); click(editor, "C++: Probe");
    check(editor.content().entities[object].components.size() == 2, "Add Component did not attach a native behaviour");
    // Saved public configuration is editable through the existing Inspector controls.
    editor.update(0, pointer()); InputField* speed = nullptr;
    for (auto& e : editor.scene().entities) if (auto* f = e.getComponent<InputField>(); f && f->text == "2") speed = f;
    check(speed && speed->onSubmit, "Native saved fields were not shown in the Inspector");
    auto submit = speed->onSubmit; submit("9"); editor.update(0, pointer());
    auto* last = dynamic_cast<const Probe*>(editor.content().entities[object].components.back().get());
    check(last && last->speed == 9, "Inspector did not persist native configuration");
    editor.save(); auto loaded = loadScene(editor.scenePath());
    check(dynamic_cast<Probe*>(loaded.entities[object].components.back().get())->speed == 9, "Edited native fields were not saved");
    check(editor.content().entities[object].getComponent<Probe>()->awakes == 0, "Editor executed a native behaviour");
    Scripts scripts; scripts.awake(loaded);
    check(loaded.entities[object].getComponent<Probe>()->awakes == 1, "Saved editor script cannot enter runtime");
}

class MouseCapture final : public MonoBehaviour {
public:
    bool captureMouse() const override { return true; }
};
TINY3D_REGISTER_BEHAVIOUR(MouseCapture)
void playChecks(const fs::path& scratch) {
    Scene scene; auto& object = scene.entities.emplace_back(); object.addComponent<Probe>().speed = 3;
    object.addComponent<MouseCapture>(); object.layer = 31;
    object.addComponent<EryScript>("using tiny3d\nmethod Awake\n Application.targetFramerate = 144\n Application.vSync = true\nmend\n"
        "method Update\n entity.transform.localPosition.y = entity.transform.localPosition.y + deltaTime\nmend\n");
    auto& camera = scene.entities.emplace_back(); camera.addComponent<Camera>().layers = layerMask(31); camera.transform.localPosition = {0, 0, -5};
    auto& falling = scene.entities.emplace_back(); falling.addComponent<Rigidbody>(); falling.transform.localPosition.y = 10;
    editor::Editor editor(std::move(scene), scratch / "PlayEngine");
    editor.change([](Scene& scene) { scene.entities.front().transform.localPosition.x = 2; });
    const auto before = serializeScene(editor.content()); const auto directory = fs::current_path();
    const int framerate = Application::targetFramerate; const bool vsync = Application::vSync;
    click(editor, "Play F5");
    check(editor.playing() && editor.captureMouse() && editor.playScene(), "Play button did not start/capture the runtime scene");
    check(Application::targetFramerate == 144 && Application::vSync, "Play did not initialize EryScript");
    rejects([&] { editor.save(); }); rejects([&] { editor.undo(); });
    auto input = pointer(); input.mouseX = 640; input.mouseY = 400; editor.update(.1f, input);
    const auto* probe = editor.playScene()->entities.front().getComponent<Probe>();
    check(probe && probe->awakes == 1 && probe->starts == 1 && probe->transform().localPosition.x > 2,
        "Play did not run native scripts on the unsaved scene");
    check(editor.playScene()->entities.front().transform.localPosition.y > 0 && editor.playScene()->entities[2].transform.localPosition.y < 10,
        "Play did not run EryScript and physics");
    check(editor.playScene()->entities[1].getComponent<Camera>()->enabled, "Play disabled the game camera");
    check(editor.scene().entities.front().layer == 31 && editor.scene().entities[1].getComponent<Camera>()->layers == layerMask(31),
        "Play toolbar changed the game's existing rendering layers");
    input.set(Key::Escape, true); editor.update(.016f, input); check(!editor.captureMouse(), "Escape did not release play capture");
    input.beginFrame(); input.set(Key::Escape, false); input.set(MouseButton::Left, true); editor.update(.016f, input);
    check(editor.captureMouse(), "Clicking the game did not restore mouse capture");
    input.beginFrame(); input.set(MouseButton::Left, false); editor.update(.016f, input);
    click(editor, "Pause"); check(editor.paused() && !editor.captureMouse(), "Pause did not pause and release the cursor");
    const auto pausedPosition = editor.playScene()->entities.front().transform.localPosition;
    const auto pausedUpdates = editor.playScene()->entities.front().getComponent<Probe>()->updates;
    editor.update(.25f, input);
    check(editor.playScene()->entities.front().getComponent<Probe>()->updates == pausedUpdates, "Paused play continued to update scripts");
    click(editor, "Step");
    check(editor.paused() && editor.playScene()->entities.front().getComponent<Probe>()->updates == pausedUpdates + 1 &&
        std::abs(editor.playScene()->entities.front().transform.localPosition.x - pausedPosition.x - 3.f / 60) < .0001f,
        "Step did not advance exactly one fixed frame");
    click(editor, "Resume"); check(!editor.paused(), "Resume did not resume play");
    click(editor, "Stop F5");
    check(!editor.playing() && !editor.captureMouse() && serializeScene(editor.content()) == before && editor.dirty(),
        "Stop did not preserve the original scene and unsaved changes");
    check(Application::targetFramerate == framerate && Application::vSync == vsync && fs::current_path() == directory,
        "Stop did not restore application settings or the working directory");
    editor.undo(); check(editor.content().entities.front().transform.localPosition.x == 0, "Play changed undo history"); editor.redo();
    editor.play(); check(editor.playScene()->entities.front().getComponent<Probe>()->awakes == 1 &&
        !editor.playScene()->entities.front().getComponent<Probe>()->updates, "Second play reused native execution state");
    input = pointer(); input.set(Key::F5, true); editor.update(.016f, input); check(!editor.playing(), "F5 did not stop play");
}

int main() {
    try {
        const auto scratch = fs::current_path() / ("behaviour-check-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        editorChecks(scratch); playChecks(scratch); lifecycleChecks(); mutationChecks(); persistenceChecks();
        std::cout << "Native behaviour lifecycle, ownership, mutation, persistence, Assets, and Inspector checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
