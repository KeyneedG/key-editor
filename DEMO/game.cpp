#include "tiny3d/project.hpp"
#include "tiny3d/physics.hpp"

#include <algorithm>

using namespace tiny3d;

class Collector final : public Game {
public:
    Collector() { reset(); }

    float fps = 0;

    void update(float seconds, const Input& input) override {
        fps = seconds > 0 ? 1 / seconds : 0;

        if (input.pressed(Key::Escape)) { quit_ = true; return; }
        if (input.pressed(Key::R)) reset();
        const auto axis = [&](Key positive, Key negative) {
            return float(input.held(positive)) - float(input.held(negative));
        };
        Transform& camera = scene_.entities.front().transform;
        constexpr float mouseSensitivity = .0025f;
        camera.rotation.y += input.mouseDeltaX * mouseSensitivity;
        camera.rotation.x = std::clamp(camera.rotation.x +
            input.mouseDeltaY * mouseSensitivity, -1.2f, 1.2f);
        const Vec3 movement{axis(Key::D, Key::A), 0, axis(Key::W, Key::S)};
        Vec3 target = camera.position + rotateY(normalized(movement), camera.rotation.y) * (seconds * 4);
        target.x = std::clamp(target.x, -10.0f, 10.0f);
        target.z = std::clamp(target.z, -6.0f, 16.0f);
        physics_.move(scene_, scene_.entities.front(), target - camera.position);
        physics_.step(scene_, seconds);
        time_ += seconds;
        for (auto index : collectibles_) {
            Entity& item = scene_.entities[index];
            item.transform.rotation = {.3f, time_, .15f};
            item.transform.position.y = .7f + .15f * std::sin(time_ * 2);
            Vec3 distance = item.transform.position - camera.position;
            distance.y = 0;
            if (item.visible && length(distance) < .9f) {
                item.visible = false;
                ++collected_;
            }
        }
    }

    const Scene& scene() const override { return scene_; }
    bool captureMouse() const override { return true; }
    bool shouldQuit() const override { return quit_; }
    std::string title() const override {
        const std::string progress = collected_ == collectibles_.size() ?
            "All cubes collected!" : "Gold cubes: " + std::to_string(collected_) + "/" + std::to_string(collectibles_.size());
        return "FPS " + std::to_string(fps) + " | " + progress + " | WASD move | Mouse look | R reset | Esc quit";
    }

private:
    void reset() {
        scene_.entities.clear();
        collectibles_.clear();
        Entity camera;
        camera.addComponent<Camera>();
        camera.addComponent<SphereCollider>().radius = .35f;
        camera.addComponent<Rigidbody>().isKinematic = true;
        camera.transform.position = {0, 1.5f, -5};
        camera.transform.rotation.x = .12f;
        scene_.entities.push_back(std::move(camera));
        collected_ = 0;
        time_ = 0;
        const auto floor = std::make_shared<Mesh>(plane());
        const auto box = std::make_shared<Mesh>(cube());
        Entity ground;
        ground.transform.position = {0, -.1f, 6};
        ground.addComponent<BoxCollider>().size = {24, .2f, 24};
        scene_.entities.push_back(std::move(ground));
        for (int z = -3; z < 9; ++z) {
            for (int x = -6; x < 6; ++x) {
                Entity tile;
                tile.transform.position = {float(x * 2 + 1), 0, float(z * 2 + 1)};
                tile.transform.scale = {2, 1, 2};
                tile.addComponent<MeshRenderer>(floor,
                    (x + z) % 2 == 0 ? Color{67, 80, 98} : Color{51, 63, 80});
                scene_.entities.push_back(std::move(tile));
            }
        }
        for (Vec3 position : {Vec3{-4, 1, 5}, Vec3{4, 1.5f, 6}, Vec3{0, .5f, 10}}) {
            Entity block;
            block.transform.position = position;
            block.transform.scale = {1.5f, position.y * 2, 1.5f};
            block.addComponent<MeshRenderer>(box, Color{72, 153, 205});
            block.addComponent<BoxCollider>();
            scene_.entities.push_back(std::move(block));
        }
        for (Vec3 position : {Vec3{-3, .7f, 0}, Vec3{3, .7f, 2}, Vec3{-2, .7f, 6},
                              Vec3{3, .7f, 9}, Vec3{0, .7f, 13}}) {
            Entity item;
            item.transform.position = position;
            item.transform.scale = {.65f, .65f, .65f};
            item.addComponent<MeshRenderer>(box, Color{255, 195, 60});
            collectibles_.push_back(scene_.entities.size());
            scene_.entities.push_back(std::move(item));
        }
    }

    Scene scene_;
    Physics physics_;
    std::vector<std::size_t> collectibles_;
    std::size_t collected_ = 0;
    float time_ = 0;
    bool quit_ = false;
};

std::unique_ptr<Game> tiny3d::createGame() {
    return std::make_unique<Collector>();
}
