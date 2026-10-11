#include "CollectorController.hpp"
#include "PlayerMovement.hpp"
#include "tiny3d/eryscript.hpp"
#include "tiny3d/ui.hpp"
#include "eryscript/reflection.hpp"
using namespace tiny3d;

Entity* CollectorController::find(const std::string& tag) const {
    for (const auto& entity : scene().entities) if (entity.tag == tag) return const_cast<Entity*>(&entity);
    return nullptr;
}
void CollectorController::Awake() {
    const auto bind = [&](const std::string& tag, std::function<void()> action) {
        if (auto* entity = find(tag)) if (auto* button = entity->getComponent<Button>()) button->onClick = std::move(action);
    };
    bind("RESUME", [this] { showMenu(Mode::Playing); });
    bind("SETTINGS", [this] { showMenu(Mode::Settings); });
    bind("QUIT", [this] { quit_ = true; });
    bind("SIZE", [this] {
        static constexpr int sizes[][2]{{800,500}, {1024,768}, {1280,720}, {1600,900}, {1920,1080}};
        std::size_t next = 0;
        for (std::size_t i = 0; i < 5; ++i) if (sizes[i][0] == pending_.width && sizes[i][1] == pending_.height) next = (i + 1) % 5;
        pending_.width = sizes[next][0]; pending_.height = sizes[next][1]; refreshSettingsLabels();
    });
    bind("MODE", [this] { pending_.fullscreen = !pending_.fullscreen; refreshSettingsLabels(); });
    bind("APPLY", [this] { screen_ = pending_; });
    bind("BACK", [this] { showMenu(Mode::Pause); });
    // Native Awake precedes EryScript initialization, including during editor play.
    for (auto& entity : scene().entities) if (entity.tag == "Collectible") if (auto* script = entity.getComponent<EryScript>())
        script->reflection().bindNativeFunctions<static_cast<double(*)(double)>(std::sin)>(script->runtime());
    refreshSettingsLabels(); showMenu(Mode::Playing);
}
void CollectorController::refreshSettingsLabels() {
    const auto text = [&](const std::string& tag, const std::string& value) {
        if (auto* button = find(tag)) if (auto* child = button->getChild(0))
            if (auto* label = child->getComponent<SimpleText>()) label->text = value;
    };
    text("SIZE", "SIZE: " + std::to_string(pending_.width) + " X " + std::to_string(pending_.height));
    text("MODE", pending_.fullscreen ? "MODE: FULLSCREEN" : "MODE: WINDOWED");
}
void CollectorController::showMenu(Mode mode) {
    mode_ = mode;
    if (auto* entity = find("MenuCanvas")) entity->visible = mode != Mode::Playing;
    if (auto* entity = find("PauseMenu")) entity->visible = mode == Mode::Pause;
    if (auto* entity = find("SettingsMenu")) entity->visible = mode == Mode::Settings;
    for (auto& entity : scene().entities) {
        if (auto* movement = entity.getComponent<PlayerMovement>()) movement->enabled = mode == Mode::Playing;
        if (entity.tag == "Collectible") if (auto* script = entity.getComponent<EryScript>()) script->enabled = mode == Mode::Playing && entity.visible;
    }
    if (mode == Mode::Settings) { pending_ = screen_; refreshSettingsLabels(); }
}
void CollectorController::resetCollectibles() {
    collected_ = 0;
    for (auto& entity : scene().entities) if (entity.tag == "Collectible") {
        entity.visible = true;
        if (auto* script = entity.getComponent<EryScript>()) { script->enabled = true; script->restart(); }
    }
}
void CollectorController::Update(float, const Input& input) {
    if (input.pressed(Key::Escape)) {
        showMenu(mode_ == Mode::Playing ? Mode::Pause : mode_ == Mode::Settings ? Mode::Pause : Mode::Playing); return;
    }
    if (!playing()) return;
    if (input.pressed(Key::R)) resetCollectibles();
    const auto* player = find("Player"); if (!player) return;
    for (auto& item : scene().entities) if (item.tag == "Collectible" && item.visible) {
        Vector3 distance = item.transform.position() - player->transform.position(); distance.y = 0;
        if (length(distance) < .9f) {
            item.visible = false; if (auto* script = item.getComponent<EryScript>()) script->enabled = false; ++collected_;
        }
    }
}
std::string CollectorController::progress() const {
    std::size_t total = 0; for (const auto& entity : scene().entities) if (entity.tag == "Collectible") ++total;
    return collected_ == total ? "All cubes collected!" : "Gold cubes: " + std::to_string(collected_) + "/" + std::to_string(total);
}
TINY3D_REGISTER_BEHAVIOUR(CollectorController)
