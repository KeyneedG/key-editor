#pragma once
#include "tiny3d/scene.hpp"
#include "tiny3d/ui.hpp"
#include <functional>
#include <optional>

namespace tiny3d::editor {
// Editor state and controls live in a separate library; the runtime knows only Game.
class Editor final : public Game {
public:
    Editor(Scene initial, std::filesystem::path engineDirectory, std::filesystem::path projectDirectory = {},
           std::filesystem::path configFile = {});
    void update(float seconds, const Input& input) override;
    Scene& scene() override { return view_; }
    const Scene& scene() const override { return view_; }
    bool executeScripts() const override { return false; }
    ScreenSettings screenSettings() const override { return {1280, 800, false}; }
    std::string title() const override;

    const Scene& content() const { return content_; }
    const std::filesystem::path& projectDirectory() const { return project_; }
    const std::filesystem::path& scenePath() const { return scenePath_; }
    bool dirty() const { return current_ != saved_; }
    void newProject(const std::string& name);
    void openProject(const std::filesystem::path& directory);
    void newScene(const std::string& name);
    void openScene(const std::filesystem::path& path);
    void save();
    void undo();
    void redo();
    void change(const std::function<void(Scene&)>& action);
    std::size_t addObject(const std::string& kind = "Empty");
    void addComponent(std::size_t object, const std::string& type);

private:
    enum class Tool { Move, Rotate, Scale };
    enum class Dialog { None, NewProject, NewScene, BuildSettings, About, AddObject, AddComponent, Unsaved };
    void rebuild();
    void replace(Scene scene, const std::filesystem::path& path);
    void rememberProject();
    void request(const std::function<void()>& action);
    void guard(const std::function<void()>& action);
    void inspect(Entity& root);
    void projectPanel(Entity& root);
    void dialog(Entity& root);
    void gizmos();
    void viewportInput(float seconds, const Input& input);
    Entity& element(Entity& parent, float x, float y, float z = -.05f);
    Entity& label(Entity& parent, float x, float y, std::string text, float size = 1.5f);
    Entity& panel(Entity& parent, float x, float y, float width, float height, Color color = {29, 34, 44});
    Entity& button(Entity& parent, float x, float y, float width, std::string text, std::function<void()> action);
    Entity& field(Entity& parent, float x, float y, float width, std::string value, std::function<void(const std::string&)> submit,
                  float height = 28, bool multiline = false);
    Entity& scroll(Entity& parent, float x, float y, float width, float height, float contentHeight, int slot);

    Scene content_, view_;
    UI ui_;
    std::filesystem::path engine_, config_, project_, assets_, folder_, scenePath_, clickedAsset_;
    std::string current_, saved_, status_ = "Ready", menu_, dialogText_;
    std::vector<std::string> undo_, redo_;
    std::optional<std::size_t> selected_;
    std::function<void()> pending_;
    Dialog dialog_ = Dialog::None;
    Tool tool_ = Tool::Move;
    Transform camera_;
    float time_ = 0, lastClick_ = -1;
    bool rebuild_ = true;
    Entity* viewportCamera_ = nullptr;
    std::array<Entity*, 3> scrolls_{};
    std::array<float, 3> offsets_{};
    std::array<Entity*, 3> handles_{};
    int dragging_ = -1;
    std::string dragBefore_;
    Vector3 dragMouse_{}, dragPosition_{}, dragScale_{};
    Vector3 dragScreenAxis_{};
    Quaternion dragRotation_{};
    float dragUnits_ = .01f;
};
// Native folder picker, starting in Engine/Projects. Returns empty on Cancel.
std::filesystem::path selectProjectFolder(const std::filesystem::path& projects);
} // namespace tiny3d::editor
