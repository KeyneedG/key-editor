#include "CollectorController.hpp"
#include "PlayerMovement.hpp"
#include "tiny3d/eryscript.hpp"
#include "eryscript/reflection.hpp"
using namespace tiny3d;

void CollectorController::Awake() {
    // Native Awake precedes EryScript initialization, including during editor play.
    if (auto* menu = getComponent<EryScript>()) menu->runtime().bind("gameplay", menu->reflection().object(this));
    for (auto& entity : scene().entities) if (entity.tag == "Collectible") if (auto* script = entity.getComponent<EryScript>())
        script->reflection().bindNativeFunctions<static_cast<double(*)(double)>(std::sin)>(script->runtime());
}
void CollectorController::setPaused(bool paused) {
    paused_ = paused;
    for (auto& entity : scene().entities) {
        if (auto* movement = entity.getComponent<PlayerMovement>()) movement->enabled = !paused;
        if (entity.tag == "Collectible") if (auto* script = entity.getComponent<EryScript>()) script->enabled = !paused && entity.visible;
    }
}
void CollectorController::resetCollectibles() {
    collected_ = 0;
    for (auto& entity : scene().entities) if (entity.tag == "Collectible") {
        entity.visible = true;
        if (auto* script = entity.getComponent<EryScript>()) { script->enabled = true; script->restart(); }
    }
}
void CollectorController::Update(float, const Input& input) {
    if (!playing()) return;
    if (input.pressed(Key::R)) resetCollectibles();
    const auto* player = scene().findWithTag("Player"); if (!player) return;
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
