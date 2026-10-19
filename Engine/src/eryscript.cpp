#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#include "eryscript/reflection.hpp"
#include <iostream>
#include <fstream>

namespace tiny3d {
namespace detail {
struct ScriptFrame {
    Entity* entity = nullptr;
    Scene* scene = nullptr;
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
#if !defined(__INTELLISENSE__)
        state->reflection.bindNamespace<^^tiny3d>(runtime_);
#endif
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
    state.scene = &scene; state.frame = {&entity, &scene, input, seconds, time_};
    try {
        if (!script.initialized_ || script.loadedSource_ != script.code || script.loadedPath_ != script.source) {
            script.initialized_ = true; script.loadedSource_ = script.code; script.loadedPath_ = script.source; script.error_.clear();
            if (!script.source.empty()) {
                std::ifstream file(script.source);
                if (!file) throw std::runtime_error("Cannot read EryScript: " + script.source);
                script.code.assign(std::istreambuf_iterator<char>(file), {});
                script.loadedSource_ = script.code;
            }
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
    // Scripts may add/remove components and objects. New scripts start on the next tick.
    std::vector<std::pair<Entity*, EryScript*>> scripts;
    std::vector<std::pair<Entity*, MonoBehaviour*>> native;
    for (Entity& entity : scene.entities) for (auto& component : entity.components) {
        if (auto* script = dynamic_cast<EryScript*>(component.get())) scripts.emplace_back(&entity, script);
        if (auto* script = dynamic_cast<MonoBehaviour*>(component.get())) native.emplace_back(&entity, script);
    }
    const auto alive = [&](Entity* entity, Component* script) {
        for (auto& item : scene.entities) if (&item == entity) {
            for (auto& component : item.components) if (component.get() == script) return true;
            return false;
        }
        return false;
    };
    const auto invoke = [&](Entity* entity, MonoBehaviour* script, auto callback) {
        if (!alive(entity, script) || !script->enabled || !script->error_.empty()) return;
        script->scene_ = &scene;
        try { callback(*script); }
        catch (const std::exception& error) {
            if (alive(entity, script)) {
                script->error_ = error.what();
                std::cerr << "C++ behaviour [" << entity->name << "]: " << script->error_ << '\n';
            }
        }
    };
    for (auto [entity, script] : native) invoke(entity, script, [](MonoBehaviour& s) {
        if (!s.awakened_) { s.awakened_ = true; s.Awake(); }
    });
    for (auto [entity, script] : scripts) if (alive(entity, script)) run(scene, *entity, *script, seconds, input, update);
    if (update) {
        for (auto [entity, script] : native) invoke(entity, script, [](MonoBehaviour& s) {
            if (s.awakened_ && !s.started_) { s.started_ = true; s.Start(); }
        });
        for (auto [entity, script] : native) invoke(entity, script, [&](MonoBehaviour& s) { if (s.started_) s.Update(seconds, input); });
        for (auto [entity, script] : native) invoke(entity, script, [&](MonoBehaviour& s) { if (s.started_) s.LateUpdate(seconds, input); });
    }
    UI{}.refresh(scene);
}
void Scripts::awake(Scene& scene) { tick(scene, 0, {}, false); }
void Scripts::update(Scene& scene, float seconds, const Input& input) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Script delta time must be finite and nonnegative");
    time_ += seconds;
    tick(scene, seconds, input, true);
}
} // namespace tiny3d
