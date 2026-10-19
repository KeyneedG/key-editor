#include "tiny3d/project.hpp"
#include "tiny3d/editor.hpp"
#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/scene.hpp"
#include "Assets/Scripts/PlayerMovement.hpp"
#include "Assets/Scripts/CollectorController.hpp"
#include <chrono>
#include <iostream>

using namespace tiny3d;
namespace fs = std::filesystem;
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
Entity& find(Scene& scene, const std::string& tag) {
    for (auto& entity : scene.entities) if (entity.tag == tag) return entity;
    throw std::runtime_error("Missing demo entity: " + tag);
}
void tick(Game& game, Scripts& scripts, float seconds, const Input& input) {
    game.update(seconds, input); scripts.update(game.scene(), seconds, input);
}
Input input() { Input i; i.focused = true; i.renderWidth = 1280; i.renderHeight = 800; i.viewport = {0, 0, 1280, 800}; i.mouseX = 640; i.mouseY = 400; return i; }
void demoChecks() {
    auto game = createGame(); auto& scene = game->scene(); auto& player = find(scene, "Player");
    auto* movement = player.getComponent<PlayerMovement>(); auto* controls = find(scene, "UICamera").getComponent<CollectorController>();
    check(movement && controls && scene.entities.size() == 156, "Saved demo scene did not supply its native components/objects");
    check(!scene.findWithTag("MenuCanvas"), "Menu objects were left in the saved scene");
    auto* menuScript = find(scene, "UICamera").getComponent<EryScript>();
    check(menuScript && menuScript->source == "Assets/Scripts/MenuUI.ery", "UI camera is missing its file-backed EryScript");
    Scripts scripts; scripts.awake(scene); auto i = input();
    check(menuScript->error().empty() && scene.entities.size() == 180 && scene.findWithTag("MenuCanvas"), "MenuUI.ery did not create the runtime UI");
    const auto start = player.transform.localPosition;
    i.set(Key::W, true); tick(*game, scripts, .1f, i);
    check(player.transform.position().z > start.z && game->captureMouse(), "Player component did not move or capture the cursor");
    for (auto& entity : scene.entities) if (auto* script = entity.getComponent<EryScript>()) check(script->error().empty(), "Saved collectible script lost its native sin binding");
    i.beginFrame(); i.set(Key::W, false); i.mouseDeltaX = 100; i.mouseDeltaY = 100; tick(*game, scripts, .016f, i);
    check(std::abs(player.transform.localRotation.y) > .01f && std::abs(player.getChild(0)->transform.localRotation.x) > .01f,
        "Player component did not rotate its owner and child camera");
    i.beginFrame(); i.set(Key::Escape, true); tick(*game, scripts, .016f, i);
    const auto pausedPosition = player.transform.position();
    check(!controls->playing() && !movement->enabled && find(scene, "MenuCanvas").visible && !game->captureMouse(), "Saved pause menu did not pause movement and release capture");
    i.beginFrame(); i.set(Key::Escape, false); i.set(Key::W, true); tick(*game, scripts, .1f, i);
    check(length(player.transform.position() - pausedPosition) < .0001f, "Player moved while the menu was open");
    auto resume = find(scene, "RESUME").getComponent<Button>()->onClick; check(bool(resume), "Saved Resume button has no runtime binding"); resume();
    i.beginFrame(); i.set(Key::W, false); i.set(Key::R, true); tick(*game, scripts, .016f, i);
    check(controls->playing() && length(player.transform.localPosition - start) < .0001f, "Component reset did not restore the player spawn");
    auto press = [&](const std::string& tag) { auto action = find(scene, tag).getComponent<Button>()->onClick; check(bool(action), "Missing EryScript callback"); action(); };
    i.beginFrame(); i.set(Key::R,false); i.set(Key::Escape,true); tick(*game,scripts,.016f,i);
    press("SETTINGS"); check(find(scene,"SettingsMenu").visible && !find(scene,"PauseMenu").visible, "Settings callback did not switch menus");
    const auto originalScreen = game->screenSettings(); press("SIZE"); press("MODE");
    check(game->screenSettings() == originalScreen, "Pending settings changed the active window before Apply");
    press("APPLY"); check(game->screenSettings().width == 1024 && game->screenSettings().fullscreen, "EryScript Apply did not commit screen settings");
    press("BACK"); press("RESUME"); check(controls->playing(), "Back/Resume callbacks failed");
    const auto count = scene.entities.size(); menuScript->restart(); scripts.awake(scene);
    check(menuScript->error().empty() && scene.entities.size() == count, "Restart duplicated generated menu objects");
    for (auto size : {std::pair{1920,1080}, {500,900}}) {
        auto resized = input(); resized.renderWidth = size.first; resized.renderHeight = size.second; resized.viewport = {0,0,size.first,size.second};
        tick(*game,scripts,0,resized); const auto* camera = find(scene,"UICamera").getComponent<Camera>();
        check(camera->orthographicSize >= 250 && camera->orthographicSize * size.first / size.second >= 399.99f, "Menu clipped at a different window aspect ratio");
    }
    i = input();
    // Move toward a saved blue block and verify collision correction remains active.
    player.transform.setPosition({-4, 0, 3}); player.transform.localRotation = {};
    i.beginFrame(); i.set(Key::R, false); tick(*game, scripts, 0, i);
    i.beginFrame(); i.set(Key::W, true); for (int n = 0; n < 20; ++n) tick(*game, scripts, .05f, i);
    check(player.transform.position().z < 4.1f, "Player component walked through a saved block collider");
    press("QUIT"); check(game->shouldQuit(), "EryScript Quit callback did not reach the game host");
}
void editorDemoChecks() {
    const auto scratch = fs::temp_directory_path() / ("tiny3d-play-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch);
    editor::Editor editor({}, scratch, TINY3D_DEMO_DIRECTORY, {}, [] { return createGame(); });
    const auto before = serializeScene(editor.content());
    editor.play(); const auto original = find(const_cast<Scene&>(*editor.playScene()), "Player").transform.position();
    auto i = input(); i.set(Key::W, true); editor.update(.1f, i);
    auto& player = find(const_cast<Scene&>(*editor.playScene()), "Player");
    check(player.transform.position().z > original.z && editor.captureMouse(), "Editor did not host the demo game and PlayerMovement component");
    Renderer renderer(1280, 800); renderer.render(editor.scene()); renderer.savePPM((scratch / "playing.ppm").string());
    i.beginFrame(); i.set(Key::W, false); i.set(Key::Escape, true); editor.update(.016f, i);
    check(!editor.captureMouse() && !find(const_cast<Scene&>(*editor.playScene()), "UICamera").getComponent<CollectorController>()->playing(),
        "Editor did not route Escape to the demo pause controller");
    renderer.render(editor.scene()); renderer.savePPM((scratch / "paused.ppm").string());
    std::function<void()> showStats;
    for (auto& e : editor.scene().entities) if (auto* text = e.getComponent<SimpleText>(); text && text->text == "Stats") showStats = e.parent()->getComponent<Button>()->onClick;
    check(bool(showStats), "Demo play is missing the Stats control"); showStats();
    editor.frameRendered({renderer.stats(), "Software", 1280, 800, .016f, .001f, .002f, .003f});
    i.beginFrame(); i.set(Key::Escape,false); editor.update(0,i);
    renderer.render(editor.scene()); renderer.savePPM((scratch / "play-stats.ppm").string());
    editor.stop(); check(serializeScene(editor.content()) == before && !editor.dirty(), "Demo playback modified the saved editor scene");
    std::cout << "Play screenshots: " << scratch.string() << '\n';
}
int main() {
    try { demoChecks(); editorDemoChecks(); std::cout << "Demo scene loading, movement, camera, collision, menus, reset, and editor play passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
