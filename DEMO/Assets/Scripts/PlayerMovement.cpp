#include "PlayerMovement.hpp"
using namespace tiny3d;

void PlayerMovement::OnGizmosSelected() {
    Gizmos::color = {90,160,255};
    Gizmos::line(transform().position(), transform().position() + transform().forward() * 2);
}
void PlayerMovement::Awake() {
    for (std::size_t i = 0; i < gameObject().childCount(); ++i) {
        auto* child = gameObject().getChild(i);
        if (child->getComponent<Camera>()) { camera_ = child; break; }
    }
    startPosition_ = transform().localPosition; startRotation_ = transform().localRotation;
    yaw_ = transform().localRotation.toEuler().y;
    if (camera_) { cameraRotation_ = camera_->transform.localRotation; pitch_ = cameraRotation_.toEuler().x; }
}
void PlayerMovement::Update(float seconds, const Input& input) {
    if (!input.focused || input.pressed(Key::Escape)) return;
    if (input.pressed(Key::R)) {
        transform().localPosition = startPosition_; transform().localRotation = startRotation_;
        yaw_ = startRotation_.toEuler().y; pitch_ = cameraRotation_.toEuler().x;
        if (camera_) camera_->transform.localRotation = cameraRotation_;
        if (auto* body = getComponent<Rigidbody>()) body->velocity = {};
        return;
    }
    yaw_ = std::remainder(yaw_ + input.mouseDeltaX * mouseSensitivity, 2 * pi);
    pitch_ = std::clamp(pitch_ + input.mouseDeltaY * mouseSensitivity, -1.2f, 1.2f);
    transform().localRotation = Quaternion::fromEuler({0, yaw_, 0});
    if (camera_) camera_->transform.localRotation = Quaternion::fromEuler({pitch_, 0, 0});
    const auto axis = [&](Key positive, Key negative) { return float(input.held(positive)) - float(input.held(negative)); };
    const Vector3 movement{axis(Key::D, Key::A), 0, axis(Key::W, Key::S)};
    Vector3 target = transform().position() + rotateY(normalized(movement), yaw_) * (seconds * speed);
    target.x = std::clamp(target.x, -10.f, 10.f); target.z = std::clamp(target.z, -6.f, 16.f);
    physics_.move(scene(), gameObject(), target - transform().position());
}
json::Value PlayerMovement::saveFields() const { return json::Value::Object{{"speed", speed}, {"mouseSensitivity", mouseSensitivity}}; }
void PlayerMovement::loadFields(const json::Value& fields) {
    if (fields.has("speed")) speed = static_cast<float>(fields.at("speed").number());
    if (fields.has("mouseSensitivity")) mouseSensitivity = static_cast<float>(fields.at("mouseSensitivity").number());
    if (!std::isfinite(speed) || speed < 0 || !std::isfinite(mouseSensitivity) || mouseSensitivity < 0)
        throw std::invalid_argument("Player speed and mouse sensitivity must be finite and nonnegative");
}
TINY3D_REGISTER_BEHAVIOUR(PlayerMovement)
