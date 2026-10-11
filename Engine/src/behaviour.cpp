#include "tiny3d/behaviour.hpp"
#include <cctype>

namespace tiny3d {
namespace {
std::vector<BehaviourType>& registry() { static std::vector<BehaviourType> types; return types; }
class MissingBehaviour final : public MonoBehaviour {
public:
    std::string script;
    json::Value fields = json::Value::Object{};
    void Awake() override { throw std::runtime_error("C++ behaviour '" + script + "' is not compiled. Rebuild the selected project."); }
    json::Value saveFields() const override { return fields; }
    void loadFields(const json::Value& value) override { value.object(); fields = value; }
};
}
Scene& MonoBehaviour::scene() {
    if (!scene_) throw std::logic_error("C++ behaviour has not entered a scene yet");
    return *scene_;
}
const Scene& MonoBehaviour::scene() const {
    if (!scene_) throw std::logic_error("C++ behaviour has not entered a scene yet");
    return *scene_;
}
const std::vector<BehaviourType>& behaviourTypes() { return registry(); }
bool registerBehaviour(BehaviourType type) {
    if (type.name.empty() || !type.create) throw std::invalid_argument("Invalid C++ behaviour registration");
    for (const auto& existing : registry()) {
        if (existing.name == type.name || existing.type == type.type)
            throw std::invalid_argument("Duplicate C++ behaviour registration: " + type.name);
    }
    registry().push_back(std::move(type));
    return true;
}
std::string behaviourName(const MonoBehaviour& behaviour) {
    if (const auto* missing = dynamic_cast<const MissingBehaviour*>(&behaviour)) return missing->script;
    for (const auto& type : registry()) if (type.type == typeid(behaviour)) return type.name;
    throw std::runtime_error("Register this C++ component with TINY3D_REGISTER_BEHAVIOUR before saving");
}
MonoBehaviour& addBehaviour(Entity& entity, const std::string& name) {
    for (const auto& type : registry()) if (type.name == name) {
        auto script = type.create(); auto& result = *script;
        entity.addComponent(std::move(script)); return result;
    }
    throw std::runtime_error("C++ behaviour is not compiled: " + std::string(name) + ". Rebuild the selected project.");
}
std::unique_ptr<MonoBehaviour> restoreBehaviour(const std::string& name, const json::Value& fields) {
    if (name.empty()) throw std::runtime_error("C++ behaviour name cannot be empty");
    fields.object();
    for (const auto& type : registry()) if (type.name == name) {
        auto script = type.create(); script->loadFields(fields); return script;
    }
    auto script = std::make_unique<MissingBehaviour>(); script->script = name; script->loadFields(fields); return script;
}
bool missingBehaviour(const MonoBehaviour& behaviour) { return dynamic_cast<const MissingBehaviour*>(&behaviour) != nullptr; }
bool isCppScript(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".c++" ||
        extension == ".h" || extension == ".hpp" || extension == ".hh" || extension == ".hxx";
}
bool Game::captureMouse() const {
    for (const auto& entity : scene().entities) for (const auto& component : entity.components)
        if (const auto* script = dynamic_cast<const MonoBehaviour*>(component.get()); script && script->enabled && script->error().empty() && script->captureMouse()) return true;
    return false;
}
} // namespace tiny3d
