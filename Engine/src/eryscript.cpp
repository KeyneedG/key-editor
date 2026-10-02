#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#include "eryscript/reflection.hpp"
#include <iostream>

namespace tiny3d {
namespace detail {
struct ScriptFrame {
    Entity* entity = nullptr;
    Input input;
    float deltaTime = 0;
    double time = 0;
};
struct ScriptBindings {
    ScriptFrame frame;
    Scene* scene = nullptr;
    eryscript::Reflection reflection;
};
} // namespace detail

EryScript::~EryScript() = default;
void EryScript::restart() { runtime_.stop(); initialized_ = false; error_.clear(); }
eryscript::Reflection& EryScript::reflection() {
    if (!bindings_) {
        bindings_ = std::make_shared<detail::ScriptBindings>();
        auto* state = bindings_.get();
        state->reflection.bindNamespace<^^tiny3d>(runtime_);
        state->reflection.validate([state](const eryscript::Object::Native& object) {
            const void* target = object.address(typeid(Entity));
            const void* component = object.address(typeid(Component));
            if (!target && !component) return;
            if (!state->scene) throw std::runtime_error("Script has not entered a scene yet");
            for (const auto& item : state->scene->entities) {
                if (target == &item) return;
                for (const auto& attached : item.components) if (component == attached.get()) return;
            }
            throw std::runtime_error("Script target was removed from the scene");
        });
        state->reflection.onError([this, state](const std::string& error) {
            error_ = error; runtime_.stop();
            std::cerr << "EryScript [" << (state->frame.entity ? state->frame.entity->tag : "uninitialized") << "]: " << error << '\n';
        });
    }
    return bindings_->reflection;
}
void Scripts::run(Scene& scene, Entity& entity, EryScript& script, float seconds, const Input& input, bool update) {
    if (!script.enabled) return;
    script.reflection();
    auto& state = *script.bindings_;
    state.scene = &scene; state.frame = {&entity, input, seconds, time_};
    try {
        if (!script.initialized_ || script.loadedSource_ != script.code) {
            script.initialized_ = true; script.loadedSource_ = script.code; script.error_.clear();
            script.runtime_.load(script.code, state.reflection.object(&std::as_const(state.frame)));
            if (script.runOnAwake && script.runtime_.hasMethod("Awake")) script.runtime_.call("Awake");
        }
        if (script.error_.empty() && script.enabled && update && script.runInUpdate && script.runtime_.hasMethod("Update")) script.runtime_.call("Update");
    } catch (const std::exception& error) {
        script.error_ = error.what(); script.runtime_.stop();
        std::cerr << "EryScript [" << entity.tag << "]: " << script.error_ << '\n';
    }
}
void Scripts::tick(Scene& scene, float seconds, const Input& input, bool update) {
    // A script may add components/entities. Iterate a snapshot so they start on the next tick.
    std::vector<std::pair<Entity*, EryScript*>> scripts;
    for (Entity& entity : scene.entities) for (auto& component : entity.components) {
        if (auto* script = dynamic_cast<EryScript*>(component.get())) scripts.emplace_back(&entity, script);
    }
    for (auto [entity, script] : scripts) run(scene, *entity, *script, seconds, input, update);
    UI{}.refresh(scene);
}
void Scripts::awake(Scene& scene) { tick(scene, 0, {}, false); }
void Scripts::update(Scene& scene, float seconds, const Input& input) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Script delta time must be finite and nonnegative");
    time_ += seconds;
    tick(scene, seconds, input, true);
}
} // namespace tiny3d
