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
    auto* movement = player.getComponent<PlayerMovement>(); auto* controls = find(scene, "MenuCanvas").getComponent<CollectorController>();
    check(movement && controls && scene.entities.size() == 180, "Saved demo scene did not supply its native components/objects");
    Scripts scripts; scripts.awake(scene); auto i = input();
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
    // Move toward a saved blue block and verify collision correction remains active.
    player.transform.setPosition({-4, 0, 3}); player.transform.localRotation = {};
    i.beginFrame(); i.set(Key::R, false); tick(*game, scripts, 0, i);
    i.beginFrame(); i.set(Key::W, true); for (int n = 0; n < 20; ++n) tick(*game, scripts, .05f, i);
    check(player.transform.position().z < 4.1f, "Player component walked through a saved block collider");
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
    check(!editor.captureMouse() && !find(const_cast<Scene&>(*editor.playScene()), "MenuCanvas").getComponent<CollectorController>()->playing(),
        "Editor did not route Escape to the demo pause controller");
    renderer.render(editor.scene()); renderer.savePPM((scratch / "paused.ppm").string());
    editor.stop(); check(serializeScene(editor.content()) == before && !editor.dirty(), "Demo playback modified the saved editor scene");
    std::cout << "Play screenshots: " << scratch.string() << '\n';
}
int main() {
    try { demoChecks(); editorDemoChecks(); std::cout << "Demo scene loading, movement, camera, collision, menus, reset, and editor play passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
