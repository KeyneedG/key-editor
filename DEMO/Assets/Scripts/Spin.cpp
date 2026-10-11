#include "tiny3d/behaviour.hpp"

// Select an object and attach "C++: Spin" in the editor's Add Component menu.
class Spin final : public tiny3d::MonoBehaviour {
public:
    float speed = 1;
    void Awake() override {
        // These access the attached object's actual properties.
        if (name() == "Empty") name() = "Spinning object";
    }
    void Update(float seconds, const tiny3d::Input&) override {
        auto& rotation = transform().localRotation;
        rotation = tiny3d::normalized(rotation * tiny3d::Quaternion::axisAngle({0, 1, 0}, speed * seconds));
    }
    tiny3d::json::Value saveFields() const override { return tiny3d::json::Value::Object{{"speed", speed}}; }
    void loadFields(const tiny3d::json::Value& fields) override {
        if (fields.has("speed")) {
            speed = static_cast<float>(fields.at("speed").number());
            if (!std::isfinite(speed)) throw std::invalid_argument("Spin speed must be finite");
        }
    }
};
TINY3D_REGISTER_BEHAVIOUR(Spin)
