#pragma once
#include "tiny3d/engine.hpp"
#include "tiny3d/gizmos.hpp"
#include "tiny3d/json.hpp"
#include <filesystem>
#include <string_view>
#include <typeindex>

namespace tiny3d {
// Native script component. Lifecycle callbacks run in game mode; gizmos run in the editor.
class MonoBehaviour : public Component {
public:
    // Enabled behaviours may request mouse capture from the default Game loop.
    virtual bool captureMouse() const { return false; }
    virtual void OnGizmos() {}
    virtual void OnGizmosSelected() {}
    virtual void Awake() {}
    virtual void Start() {}
    virtual void Update(float seconds, const Input& input) { (void)seconds; (void)input; }
    virtual void LateUpdate(float seconds, const Input& input) { (void)seconds; (void)input; }
    // Save editable configuration, not transient callback state. Fields must be a JSON object.
    virtual json::Value saveFields() const { return json::Value::Object{}; }
    virtual void loadFields(const json::Value& fields) {
        if (!fields.object().empty()) throw std::runtime_error("This C++ behaviour does not support saved fields");
    }
    Scene& scene();
    const Scene& scene() const;
    const std::string& error() const { return error_; }

private:
    friend class Scripts;
    friend class Gizmos;
    Scene* scene_ = nullptr;
    bool awakened_ = false, started_ = false;
    std::string error_;
};

struct BehaviourType {
    std::string name;
    std::filesystem::path source;
    std::type_index type;
    std::unique_ptr<MonoBehaviour> (*create)();
};
const std::vector<BehaviourType>& behaviourTypes();
bool registerBehaviour(BehaviourType type);
template<class T> requires std::derived_from<T, MonoBehaviour> && std::default_initializable<T>
bool registerBehaviour(std::string name, std::filesystem::path source = {}) {
    return registerBehaviour({std::move(name), std::move(source), typeid(T),
        []() -> std::unique_ptr<MonoBehaviour> { return std::make_unique<T>(); }});
}
std::string behaviourName(const MonoBehaviour& behaviour);
MonoBehaviour& addBehaviour(Entity& entity, const std::string& name);
// Uncompiled types retain their identity and saved fields when opened by another editor build.
std::unique_ptr<MonoBehaviour> restoreBehaviour(const std::string& name, const json::Value& fields);
bool missingBehaviour(const MonoBehaviour& behaviour);
bool isCppScript(const std::filesystem::path& path);
} // namespace tiny3d

// Place once in a .cpp file, after the class definition. Names must be unique per project.
#define TINY3D_DETAIL_JOIN_IMPL(a, b) a##b
#define TINY3D_DETAIL_JOIN(a, b) TINY3D_DETAIL_JOIN_IMPL(a, b)
#define TINY3D_REGISTER_BEHAVIOUR(Type) \
    namespace { [[maybe_unused]] const bool TINY3D_DETAIL_JOIN(tiny3dBehaviour_, __COUNTER__) = \
        ::tiny3d::registerBehaviour<Type>(#Type, __FILE__); }
