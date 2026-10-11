#include "tiny3d/project.hpp"
#include "tiny3d/scene.hpp"
#include "tiny3d/json.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#include "Assets/Scripts/CollectorController.hpp"
#include <fstream>

using namespace tiny3d;

// The saved scene supplies every object and component. Project code only hosts the loop.
class Collector final : public Game {
public:
    Collector() {
        std::ifstream in("project.json");
        if (!in) throw std::runtime_error("Cannot read demo project.json");
        const auto settings = json::parse(std::string((std::istreambuf_iterator<char>(in)), {}));
        scene_ = tiny3d::loadScene(settings.at("startupScene").string());
    }
    void update(float seconds, const Input& input) override {
        fps_ = seconds > 0 ? 1 / seconds : 0;
        ui_.update(scene_, input);
        if (const auto* controls = controller(); !controls || controls->playing()) physics_.step(scene_, seconds);
    }
    Scene& scene() override { return scene_; }
    const Scene& scene() const override { return scene_; }
    bool captureMouse() const override { const auto* controls = controller(); return controls && controls->playing(); }
    bool shouldQuit() const override { const auto* controls = controller(); return controls && controls->quitting(); }
    ScreenSettings screenSettings() const override { const auto* controls = controller(); return controls ? controls->screenSettings() : ScreenSettings{800, 500, false}; }
    std::string title() const override {
        const auto* controls = controller();
        return "FPS " + std::to_string(int(fps_)) + " | " + (controls ? controls->progress() : "Collector") +
            " | WASD move | Mouse look | R reset | Esc menu";
    }
private:
    const CollectorController* controller() const {
        for (const auto& entity : scene_.entities) if (auto* controls = entity.getComponent<CollectorController>()) return controls;
        return nullptr;
    }
    Scene scene_;
    UI ui_;
    Physics physics_;
    float fps_ = 0;
};
std::unique_ptr<Game> tiny3d::createGame() { return std::make_unique<Collector>(); }
