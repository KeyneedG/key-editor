#pragma once
#include "tiny3d/behaviour.hpp"
#include "tiny3d/physics.hpp"

class PlayerMovement final : public tiny3d::MonoBehaviour {
public:
    float speed = 4, mouseSensitivity = .0025f;
    bool captureMouse() const override { return true; }
    void Awake() override;
    void OnGizmosSelected() override;
    void Update(float seconds, const tiny3d::Input& input) override;
    tiny3d::json::Value saveFields() const override;
    void loadFields(const tiny3d::json::Value& fields) override;
private:
    tiny3d::Entity* camera_ = nullptr;
    tiny3d::Vector3 startPosition_;
    tiny3d::Quaternion startRotation_, cameraRotation_;
    float yaw_ = 0, pitch_ = 0;
    tiny3d::Physics physics_;
};
