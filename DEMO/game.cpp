#include "tiny3d/project.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#include "tiny3d/eryscript.hpp"

#include <algorithm>

using namespace tiny3d;

class Collector final : public Game {
public:
    Collector() { reset(); }

    float fps = 0;

    void update(float seconds, const Input& input) override {
        fps = seconds > 0 ? 1 / seconds : 0;

        if (input.pressed(Key::Escape)) {
            showMenu(mode_ == Mode::Playing ? Mode::Pause : mode_ == Mode::Settings ? Mode::Pause : Mode::Playing);
            ui_.update(scene_, input);
            return;
        }
        const bool wasPlaying = mode_ == Mode::Playing;
        ui_.update(scene_, input);
        if (!wasPlaying || mode_ != Mode::Playing || quit_) return;
        if (input.pressed(Key::R)) reset();
        const auto axis = [&](Key positive, Key negative) {
            return float(input.held(positive)) - float(input.held(negative));
        };
        Entity& player = scene_.entities[0];
        Transform& camera = scene_.entities[1].transform;
        constexpr float mouseSensitivity = .0025f;
        yaw_ += input.mouseDeltaX * mouseSensitivity;
        pitch_ = std::clamp(pitch_ + input.mouseDeltaY * mouseSensitivity, -1.2f, 1.2f);
        player.transform.localRotation = Quaternion::fromEuler({0, yaw_, 0});
        camera.localRotation = Quaternion::fromEuler({pitch_, 0, 0});
        const Vector3 movement{axis(Key::D, Key::A), 0, axis(Key::W, Key::S)};
        Vector3 target = player.transform.position() + rotateY(normalized(movement), yaw_) * (seconds * 4);
        target.x = std::clamp(target.x, -10.0f, 10.0f);
        target.z = std::clamp(target.z, -6.0f, 16.0f);
        physics_.move(scene_, player, target - player.transform.position());
        physics_.step(scene_, seconds);
        for (auto index : collectibles_) {
            Entity& item = scene_.entities[index];
            Vector3 distance = item.transform.position() - camera.position();
            distance.y = 0;
            if (item.visible && length(distance) < .9f) {
                item.visible = false;
                item.getComponent<EryScript>()->enabled = false;
                ++collected_;
            }
        }
    }

    Scene& scene() override { return scene_; }
    const Scene& scene() const override { return scene_; }
    bool captureMouse() const override { return mode_ == Mode::Playing; }
    ScreenSettings screenSettings() const override { return screen_; }
    bool shouldQuit() const override { return quit_; }
    std::string title() const override {
        const std::string progress = collected_ == collectibles_.size() ?
            "All cubes collected!" : "Gold cubes: " + std::to_string(collected_) + "/" + std::to_string(collectibles_.size());
        return "FPS " + std::to_string(fps) + " | " + progress + " | WASD move | Mouse look | R reset | Esc menu";
    }

private:
    enum class Mode { Playing, Pause, Settings };

    Entity& makeUI(Entity& parent, Vector3 position, std::string tag) {
        Entity& entity = scene_.entities.emplace_back();
        entity.layer = uiLayer;
        entity.tag = std::move(tag);
        entity.setParent(&parent, false);
        entity.transform.localPosition = position;
        return entity;
    }

    Entity& makeText(Entity& parent, Vector3 position, const std::string& text, float pixelSize = 2.5f) {
        Entity& entity = makeUI(parent, position, "Label");
        auto& label = entity.addComponent<SimpleText>();
        label.text = text;
        label.pixelSize = pixelSize;
        return entity;
    }

    Entity& makeButton(Entity& parent, float y, const std::string& label, std::function<void()> action) {
        Entity& entity = makeUI(parent, {0, y, -.1f}, label);
        auto& image = entity.addComponent<Image>();
        image.width = 300; image.height = 44;
        entity.addComponent<Button>().onClick = std::move(action);
        makeText(entity, {0, 0, -.02f}, label);
        return entity;
    }

    void makePanel(Entity& parent, float width, float height) {
        Entity& panel = makeUI(parent, {}, "Panel");
        auto& image = panel.addComponent<Image>();
        image.width = width; image.height = height;
        image.color = {24, 31, 46};
    }

    void refreshSettingsLabels() {
        sizeLabel_->getComponent<SimpleText>()->text = "SIZE: " + std::to_string(pending_.width) +
            " X " + std::to_string(pending_.height);
        modeLabel_->getComponent<SimpleText>()->text = pending_.fullscreen ? "MODE: FULLSCREEN" : "MODE: WINDOWED";
    }

    void showMenu(Mode mode) {
        ui_.cancel();
        mode_ = mode;
        canvas_->visible = mode != Mode::Playing;
        pauseMenu_->visible = mode == Mode::Pause;
        settingsMenu_->visible = mode == Mode::Settings;
        for (auto index : collectibles_) {
            Entity& item = scene_.entities[index];
            item.getComponent<EryScript>()->enabled = mode == Mode::Playing && item.visible;
        }
        if (mode == Mode::Settings) { pending_ = screen_; refreshSettingsLabels(); }
    }

    void createMenus() {
        Entity& camera = scene_.entities.emplace_back();
        camera.tag = "UICamera";
        camera.layer = uiLayer;
        auto& view = camera.addComponent<Camera>();
        view.depth = 10;
        view.layers = layerMask(uiLayer);
        view.clearColor = false;
        view.orthographic = true;
        view.orthographicSize = 250;
        canvas_ = &makeUI(camera, {0, 0, 5}, "MenuCanvas");
        canvas_->addComponent<Canvas>().camera = &camera;
        pauseMenu_ = &makeUI(*canvas_, {}, "PauseMenu");
        makePanel(*pauseMenu_, 360, 340);
        makeText(*pauseMenu_, {0, 115, -.1f}, "PAUSED", 4);
        makeButton(*pauseMenu_, 45, "RESUME", [this] { showMenu(Mode::Playing); });
        makeButton(*pauseMenu_, -15, "SETTINGS", [this] { showMenu(Mode::Settings); });
        makeButton(*pauseMenu_, -75, "QUIT", [this] { quit_ = true; });
        makeText(*pauseMenu_, {0, -135, -.1f}, "ESC TO RESUME", 1.5f);

        settingsMenu_ = &makeUI(*canvas_, {}, "SettingsMenu");
        makePanel(*settingsMenu_, 420, 410);
        makeText(*settingsMenu_, {0, 160, -.1f}, "SETTINGS", 3.5f);
        makeText(*settingsMenu_, {0, 110, -.1f}, "CLICK TO CHANGE", 1.5f);
        Entity& size = makeButton(*settingsMenu_, 65, "SIZE", [this] {
            static constexpr int sizes[][2]{{800,500}, {1024,768}, {1280,720}, {1600,900}, {1920,1080}};
            std::size_t nextSize = 0;
            for (std::size_t i = 0; i < 5; ++i) {
                if (sizes[i][0] == pending_.width && sizes[i][1] == pending_.height) nextSize = (i + 1) % 5;
            }
            pending_.width = sizes[nextSize][0]; pending_.height = sizes[nextSize][1];
            refreshSettingsLabels();
        });
        sizeLabel_ = size.getChild(0);
        Entity& mode = makeButton(*settingsMenu_, 5, "MODE", [this] {
            pending_.fullscreen = !pending_.fullscreen;
            refreshSettingsLabels();
        });
        modeLabel_ = mode.getChild(0);
        makeButton(*settingsMenu_, -55, "APPLY", [this] { screen_ = pending_; });
        makeButton(*settingsMenu_, -115, "BACK", [this] { showMenu(Mode::Pause); });
        makeText(*settingsMenu_, {0, -175, -.1f}, "FULLSCREEN IS BORDERLESS", 1.5f);
        pending_ = screen_;
        refreshSettingsLabels();
        showMenu(Mode::Playing);
        ui_.update(scene_, {});
    }

    void reset() {
        ui_.cancel();
        scene_.entities.clear();
        collectibles_.clear();
        Entity& player = scene_.entities.emplace_back();
        player.tag = "Player";
        player.transform.localPosition = {0, 0, -5};
        auto& capsule = player.addComponent<CapsuleCollider>();
        capsule.center = {0, .9f, 0};
        capsule.radius = .35f;
        capsule.height = 1.8f;
        player.addComponent<Rigidbody>().isKinematic = true;
        Entity& camera = scene_.entities.emplace_back();
        camera.tag = "WorldCamera";
        camera.addComponent<Camera>().layers = layerMask(0);
        camera.setParent(&player, false);
        camera.transform.localPosition = {0, 1.5f, 0};
        yaw_ = 0;
        pitch_ = .12f;
        camera.transform.localRotation = Quaternion::fromEuler({pitch_, 0, 0});
        collected_ = 0;
        const auto floor = std::make_shared<Mesh>(plane());
        const auto box = std::make_shared<Mesh>(cube());
        Entity ground;
        ground.transform.localPosition = {0, -.1f, 6};
        ground.addComponent<BoxCollider>().size = {24, .2f, 24};
        scene_.entities.push_back(std::move(ground));
        for (int z = -3; z < 9; ++z) {
            for (int x = -6; x < 6; ++x) {
                Entity tile;
                tile.transform.localPosition = {float(x * 2 + 1), 0, float(z * 2 + 1)};
                tile.transform.localScale = {2, 1, 2};
                tile.addComponent<MeshRenderer>(floor,
                    (x + z) % 2 == 0 ? Color{67, 80, 98} : Color{51, 63, 80});
                scene_.entities.push_back(std::move(tile));
            }
        }
        for (Vector3 position : {Vector3{-4, 1, 5}, Vector3{4, 1.5f, 6}, Vector3{0, .5f, 10}}) {
            Entity block;
            block.transform.localPosition = position;
            block.transform.localScale = {1.5f, position.y * 2, 1.5f};
            block.addComponent<MeshRenderer>(box, Color{72, 153, 205});
            block.addComponent<BoxCollider>();
            scene_.entities.push_back(std::move(block));
        }
        for (Vector3 position : {Vector3{-3, .7f, 0}, Vector3{3, .7f, 2}, Vector3{-2, .7f, 6},
                              Vector3{3, .7f, 9}, Vector3{0, .7f, 13}}) {
            Entity item;
            item.tag = "Collectible";
            item.transform.localPosition = position;
            item.transform.localScale = {.65f, .65f, .65f};
            item.addComponent<MeshRenderer>(box, Color{255, 195, 60});
            item.addComponent<EryScript>(R"ery(
using Tiny3D
specify name = "Floating collectible"
var age = 0

method Awake
    transform.localRotation = Quaternion.FromEuler(0.3, 0, 0.15)
mend

method Update
    age = age + Time.deltaTime
    var position = transform.localPosition
    position.y = 0.7 + 0.15 * Math.Sin(age * 2)
    transform.localPosition = position
    transform.localRotation = Quaternion.FromEuler(0.3, age, 0.15)
mend
)ery");
            collectibles_.push_back(scene_.entities.size());
            scene_.entities.push_back(std::move(item));
        }
        createMenus();
    }

    static constexpr unsigned uiLayer = 1;
    Scene scene_;
    Physics physics_;
    UI ui_;
    ScreenSettings screen_{800, 500, false}, pending_{};
    Mode mode_ = Mode::Playing;
    Entity* canvas_ = nullptr;
    Entity* pauseMenu_ = nullptr;
    Entity* settingsMenu_ = nullptr;
    Entity* sizeLabel_ = nullptr;
    Entity* modeLabel_ = nullptr;
    std::vector<std::size_t> collectibles_;
    std::size_t collected_ = 0;
    float yaw_ = 0, pitch_ = .12f;
    bool quit_ = false;
};

std::unique_ptr<Game> tiny3d::createGame() {
    return std::make_unique<Collector>();
}
