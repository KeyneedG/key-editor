#include "tiny3d/engine.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace tiny3d;

class Collector final : public Game {
public:
    Collector() { reset(); }

    void update(float seconds, const Input& input) override {
        if (input.pressed(Key::Reset)) reset();
        const auto axis = [&](Key positive, Key negative) {
            return float(input.held(positive)) - float(input.held(negative));
        };
        camera_.rotation.y += axis(Key::LookRight, Key::LookLeft) * seconds * 1.6f;
        camera_.rotation.x = std::clamp(camera_.rotation.x +
            axis(Key::LookDown, Key::LookUp) * seconds * 1.6f, -1.2f, 1.2f);
        const Vec3 movement{axis(Key::Right, Key::Left), 0, axis(Key::Forward, Key::Backward)};
        camera_.position = camera_.position + rotateY(normalized(movement), camera_.rotation.y) * (seconds * 4);
        camera_.position.x = std::clamp(camera_.position.x, -10.0f, 10.0f);
        camera_.position.z = std::clamp(camera_.position.z, -6.0f, 16.0f);
        time_ += seconds;
        for (auto index : collectibles_) {
            Entity& item = scene_.entities[index];
            item.transform.rotation = {.3f, time_, .15f};
            item.transform.position.y = .7f + .15f * std::sin(time_ * 2);
            Vec3 distance = item.transform.position - camera_.position;
            distance.y = 0;
            if (item.visible && length(distance) < .9f) {
                item.visible = false;
                ++collected_;
            }
        }
    }

    const Scene& scene() const override { return scene_; }
    const Camera& camera() const override { return camera_; }
    std::string title() const override {
        const std::string progress = collected_ == collectibles_.size() ?
            "All cubes collected!" : "Gold cubes: " + std::to_string(collected_) + "/" + std::to_string(collectibles_.size());
        return "Tiny3D | " + progress + " | WASD move | Arrows look | R reset | Esc quit";
    }

private:
    void reset() {
        scene_.entities.clear();
        collectibles_.clear();
        camera_ = {};
        camera_.position = {0, 1.5f, -5};
        camera_.rotation.x = .12f;
        collected_ = 0;
        time_ = 0;
        const auto floor = std::make_shared<Mesh>(plane());
        const auto box = std::make_shared<Mesh>(cube());
        for (int z = -3; z < 9; ++z) {
            for (int x = -6; x < 6; ++x) {
                Entity tile{floor};
                tile.transform.position = {float(x * 2 + 1), 0, float(z * 2 + 1)};
                tile.transform.scale = {2, 1, 2};
                tile.color = (x + z) % 2 == 0 ? Color{67, 80, 98} : Color{51, 63, 80};
                scene_.entities.push_back(tile);
            }
        }
        for (Vec3 position : {Vec3{-4, 1, 5}, Vec3{4, 1.5f, 6}, Vec3{0, .5f, 10}}) {
            Entity block{box};
            block.transform.position = position;
            block.transform.scale = {1.5f, position.y * 2, 1.5f};
            block.color = {72, 153, 205};
            scene_.entities.push_back(block);
        }
        for (Vec3 position : {Vec3{-3, .7f, 0}, Vec3{3, .7f, 2}, Vec3{-2, .7f, 6},
                              Vec3{3, .7f, 9}, Vec3{0, .7f, 13}}) {
            Entity item{box};
            item.transform.position = position;
            item.transform.scale = {.65f, .65f, .65f};
            item.color = {255, 195, 60};
            collectibles_.push_back(scene_.entities.size());
            scene_.entities.push_back(item);
        }
    }

    Scene scene_;
    Camera camera_;
    std::vector<std::size_t> collectibles_;
    std::size_t collected_ = 0;
    float time_ = 0;
};

int main(int argc, char** argv) {
    try {
        Collector game;
        if (argc == 3 && std::string(argv[1]) == "--render") {
            game.update(.5f, {});
            Renderer renderer(800, 500);
            renderer.render(game.scene(), game.camera());
            renderer.savePPM(argv[2]);
            std::cout << "Saved " << argv[2] << '\n';
            return 0;
        }
        unsigned frames = 0;
        if (argc == 3 && std::string(argv[1]) == "--frames") {
            const std::string count = argv[2];
            if (count.empty() || count.find_first_not_of("0123456789") != std::string::npos) {
                throw std::invalid_argument("Frame count must be a positive integer");
            }
            const auto value = std::stoul(count);
            if (value == 0 || value > 1000000) throw std::invalid_argument("Frame count must be between 1 and 1000000");
            frames = static_cast<unsigned>(value);
        } else if (argc != 1) {
            std::cerr << "Usage: demo [--render file.ppm | --frames count]\n";
            return 1;
        }
        return run(game, 800, 500, frames);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
