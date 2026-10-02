#pragma once

#include "tiny3d/engine.hpp"
#include "eryscript/eryscript.hpp"

namespace eryscript { class Reflection; }

namespace tiny3d {
namespace detail { struct ScriptBindings; }

struct EryScript : Component {
    std::string code;
    bool runOnAwake = true;
    bool runInUpdate = true;

    explicit EryScript(std::string code = {}, bool runOnAwake = true, bool runInUpdate = true)
        : code(std::move(code)), runOnAwake(runOnAwake), runInUpdate(runInUpdate) {}
    ~EryScript() override;
    eryscript::Runtime& runtime() { return runtime_; }
    eryscript::Reflection& reflection(); // Discover additional project namespaces before Awake.
    const eryscript::Runtime& runtime() const { return runtime_; }
    const std::string& error() const { return error_; }
    void restart(); // Reset variables and run Awake again on the next scripting tick.

private:
    friend class Scripts;
    eryscript::Runtime runtime_;
    std::shared_ptr<detail::ScriptBindings> bindings_;
    std::string loadedSource_, error_;
    bool initialized_ = false;
};

// The native loop and --render call this automatically. Also usable with a custom loop.
class Scripts {
public:
    void awake(Scene& scene);
    void update(Scene& scene, float seconds, const Input& input);
private:
    void run(Scene& scene, Entity& entity, EryScript& script, float seconds, const Input& input, bool update);
    void tick(Scene& scene, float seconds, const Input& input, bool update);
    double time_ = 0;
};
} // namespace tiny3d
