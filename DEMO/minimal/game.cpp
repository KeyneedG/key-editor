#include "tiny3d/project.hpp"

class MyGame final : public tiny3d::Game {
    tiny3d::Scene scene_;
    tiny3d::Camera camera_;
public:
    MyGame() {
        camera_.position = {0, 0, -3};
        scene_.entities.push_back({std::make_shared<tiny3d::Mesh>(tiny3d::cube())});
    }
    void update(float seconds, const tiny3d::Input&) override {
        scene_.entities[0].transform.rotation.y += seconds;
    }
    const tiny3d::Scene& scene() const override { return scene_; }
    const tiny3d::Camera& camera() const override { return camera_; }
};

std::unique_ptr<tiny3d::Game> tiny3d::createGame() {
    return std::make_unique<MyGame>();
}
