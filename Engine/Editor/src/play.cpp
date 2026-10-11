#include "tiny3d/editor.hpp"
#include "tiny3d/physics.hpp"
#include <limits>
#include <unordered_map>

namespace tiny3d::editor {
namespace {
class PreviewGame final : public Game {
public:
    void update(float seconds, const Input& input) override { ui_.update(scene_, input); physics_.step(scene_, seconds); }
    Scene& scene() override { return scene_; }
    const Scene& scene() const override { return scene_; }
private:
    Scene scene_;
    UI ui_;
    Physics physics_;
};
// Presentation contains only render data and toolbar controls. Runtime scripts keep their
// original owners, cameras, layers, callbacks, and state in the separate game scene.
void appendView(Scene& target, const Scene& source, bool toolbar, unsigned overlayLayer) {
    std::unordered_map<const Entity*, Entity*> entities;
    for (const auto& original : source.entities) {
        auto& copy = target.entities.emplace_back(); entities[&original] = &copy;
        copy.name = original.name; copy.tag = original.tag; copy.layer = toolbar ? static_cast<std::uint8_t>(overlayLayer) : original.layer;
        copy.visible = original.visible; copy.transform = original.transform;
        for (const auto& component : original.components) {
            Component* added = nullptr;
            if (const auto* mesh = dynamic_cast<const MeshRenderer*>(component.get())) {
                auto& visual = copy.addComponent<MeshRenderer>(mesh->mesh, mesh->color); visual.unlit = mesh->unlit; added = &visual;
            } else if (const auto* camera = dynamic_cast<const Camera*>(component.get())) {
                auto& view = copy.addComponent<Camera>(); added = &view;
                view.depth = camera->depth; view.layers = toolbar ? layerMask(overlayLayer) : camera->layers & ~layerMask(overlayLayer);
                view.clearColor = camera->clearColor; view.orthographic = camera->orthographic;
                view.orthographicSize = camera->orthographicSize; view.fieldOfView = camera->fieldOfView;
                view.nearPlane = camera->nearPlane; view.farPlane = camera->farPlane;
            } else if (toolbar) {
                if (const auto* text = dynamic_cast<const SimpleText*>(component.get())) {
                    auto& label = copy.addComponent<SimpleText>(); label.text = text->text; label.color = text->color; label.pixelSize = text->pixelSize; label.centered = text->centered; added = &label;
                } else if (const auto* button = dynamic_cast<const Button*>(component.get())) {
                    copy.addComponent<Button>().onClick = button->onClick;
                }
            }
            if (added) added->enabled = component->enabled;
        }
    }
    for (const auto& original : source.entities) if (original.parent())
        entities.at(&original)->setParent(entities.at(original.parent()), false);
}
}
Editor::~Editor() {
    if (playing()) {
        Application::targetFramerate = editTargetFramerate_; Application::vSync = editVSync_;
        std::error_code ignored; std::filesystem::current_path(editDirectory_, ignored);
    }
}
void Editor::requireEditing() const {
    if (playing()) throw std::runtime_error("Stop play mode before editing or saving the scene");
}
void Editor::play() {
    if (playing()) return;
    if (dragging_ >= 0) throw std::runtime_error("Finish the transform drag before playing");
    const auto directory = std::filesystem::current_path();
    const auto framerate = Application::targetFramerate; const auto vsync = Application::vSync;
    try {
        if (!project_.empty()) std::filesystem::current_path(project_);
        auto game = playFactory_ && project_ == factoryProject_ ? playFactory_() : std::make_unique<PreviewGame>();
        if (!game) throw std::runtime_error("Play factory returned no game");
        game->restoreScene(current_);
        for (int i = 0; i < 3; ++i) if (scrolls_[i]) offsets_[i] = scrolls_[i]->getComponent<ScrollRect>()->offset;
        ui_.cancel(); scrolls_.fill(nullptr); handles_.fill(nullptr);
        viewportCamera_ = gizmoCamera_ = statsPanel_ = nullptr; statsText_ = nullptr;
        looking_ = false; menu_.clear(); dialog_ = Dialog::None; pending_ = {};
        editDirectory_ = directory; editTargetFramerate_ = framerate; editVSync_ = vsync;
        playGame_ = std::move(game); playScripts_ = Scripts{};
        playPaused_ = playStep_ = false; playFocused_ = true;
        buildPlayControls();
        if (playGame_->executeScripts()) playScripts_.awake(playGame_->scene());
        refreshPlayView();
    } catch (...) {
        playGame_.reset(); ui_.cancel(); playControls_.entities.clear(); pauseLabel_ = nullptr;
        Application::targetFramerate = framerate; Application::vSync = vsync;
        std::error_code ignored; std::filesystem::current_path(directory, ignored); rebuild_ = true;
        throw;
    }
}
void Editor::stop() {
    if (!playing()) return;
    ui_.cancel(); playGame_.reset(); playControls_.entities.clear(); pauseLabel_ = nullptr;
    playPaused_ = playStep_ = playFocused_ = false;
    Application::targetFramerate = editTargetFramerate_; Application::vSync = editVSync_;
    std::error_code ignored; std::filesystem::current_path(editDirectory_, ignored);
    status_ = "Stopped | Scene edits preserved"; rebuild_ = true; rebuild();
}
void Editor::buildPlayControls() {
    Scene editView = std::move(view_); view_ = {};
    auto& camera = view_.entities.emplace_back(); camera.layer = 31;
    auto& view = camera.addComponent<Camera>(); view.layers = layerMask(31); view.clearColor = false;
    view.depth = std::numeric_limits<float>::max(); view.orthographic = true; view.orthographicSize = 400;
    auto& root = element(camera, 0, 0, 10); root.addComponent<Canvas>().camera = &camera;
    auto& bar = panel(root, 0, 382, 1280, 36, {22, 27, 36});
    label(bar, -450, 0, "PLAY MODE", 1.6f);
    button(bar, -130, 0, 110, "Stop F5", [this] { stop(); });
    auto& pause = button(bar, 0, 0, 110, "Pause", [this] {
        playPaused_ = !playPaused_; playFocused_ = !playPaused_;
        pauseLabel_->text = playPaused_ ? "Resume" : "Pause";
    });
    pauseLabel_ = pause.getChild(0)->getComponent<SimpleText>();
    button(bar, 130, 0, 110, "Step", [this] {
        playPaused_ = true; playStep_ = true; playFocused_ = false; pauseLabel_->text = "Resume";
    });
    label(bar, 424, 0, "ESC RELEASES MOUSE", 1.1f);
    ui_.refresh(view_); playControls_ = std::move(view_); view_ = std::move(editView);
}
void Editor::refreshPlayView() {
    std::uint32_t usedLayers = 0;
    for (const auto& entity : playGame_->scene().entities) usedLayers |= layerMask(entity.layer);
    unsigned overlayLayer = 32;
    for (int layer = 31; layer >= 0; --layer) if (!(usedLayers & layerMask(unsigned(layer)))) { overlayLayer = unsigned(layer); break; }
    Scene frame; appendView(frame, playGame_->scene(), false, overlayLayer);
    // If a scene uses every layer, keep all game content visible; F5 still stops play.
    if (overlayLayer < 32) appendView(frame, playControls_, true, overlayLayer);
    view_ = std::move(frame); rebuild_ = false;
}
void Editor::updatePlay(float seconds, const Input& input) {
    if (input.focused && input.pressed(Key::F5)) { stop(); return; }
    const bool captured = captureMouse();
    ui_.update(playControls_, input);
    if (!playing()) return;
    const bool overGame = input.viewport.width > 0 && input.viewport.height > 0 &&
        input.mouseX >= input.viewport.x && input.mouseX < input.viewport.x + input.viewport.width &&
        input.mouseY >= input.viewport.y + input.viewport.height * 36 / 800 && input.mouseY < input.viewport.y + input.viewport.height;
    if (!input.focused) playFocused_ = false;
    const bool beginFocus = input.focused && overGame && input.pressed(MouseButton::Left) && !playFocused_;
    if (beginFocus) playFocused_ = true;
    Input gameInput = input;
    if (!overGame && !captured) gameInput.releaseAll();
    else if (!playFocused_ || beginFocus) {
        gameInput.keys = {}; gameInput.mouseDeltaX = gameInput.mouseDeltaY = 0; gameInput.text.clear();
    }
    if (!playPaused_ || playStep_) {
        const float elapsed = playStep_ ? 1.f / 60 : seconds;
        if (playStep_) { gameInput.releaseAll(); playStep_ = false; }
        playGame_->update(elapsed, gameInput);
        if (playGame_->shouldQuit()) { stop(); return; }
        if (playGame_->executeScripts()) playScripts_.update(playGame_->scene(), elapsed, gameInput);
        if (playGame_->shouldQuit()) { stop(); return; }
    }
    if (input.pressed(Key::Escape)) playFocused_ = false;
    refreshPlayView();
}
} // namespace tiny3d::editor
