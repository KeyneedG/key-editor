#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"

#include <iostream>
#include <limits>
#include <type_traits>

namespace tiny3d {
struct ScriptBindings {
    Scene* scene = nullptr;
    Entity* owner = nullptr;
    double time = 0;
    float deltaTime = 0;
    Input input;
    std::function<void(const std::string&)> failed;
};
namespace {
using eryscript::Value;
using eryscript::Object;
using eryscript::Arguments;
using eryscript::Function;
using Bindings = std::shared_ptr<ScriptBindings>;
Value entityObject(const Bindings& bindings, Entity* target);

Value pack(Vector3 value) {
    auto object = std::make_shared<Object>();
    object->fields = {{"x", value.x}, {"y", value.y}, {"z", value.z}};
    return object;
}
Value pack(Quaternion value) {
    auto object = std::make_shared<Object>();
    object->fields = {{"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w}};
    return object;
}
Value pack(Color value) {
    auto object = std::make_shared<Object>();
    object->fields = {{"r", int(value.r)}, {"g", int(value.g)}, {"b", int(value.b)}};
    return object;
}
template<class T> Value pack(const T& value) {
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) return double(value);
    else return Value(value);
}
template<class T> T unpack(const Value& value) {
    if constexpr (std::is_same_v<T, Vector3>) {
        const auto object = value.object();
        return {unpack<float>(object->get("x")), unpack<float>(object->get("y")), unpack<float>(object->get("z"))};
    } else if constexpr (std::is_same_v<T, Quaternion>) {
        const auto object = value.object();
        return {unpack<float>(object->get("x")), unpack<float>(object->get("y")), unpack<float>(object->get("z")), unpack<float>(object->get("w"))};
    } else if constexpr (std::is_same_v<T, Color>) {
        const auto object = value.object();
        return {unpack<std::uint8_t>(object->get("r")), unpack<std::uint8_t>(object->get("g")), unpack<std::uint8_t>(object->get("b"))};
    } else if constexpr (std::is_same_v<T, bool>) return value.boolean();
    else if constexpr (std::is_same_v<T, std::string>) return value.string();
    else {
        const double n = value.number();
        if (!std::isfinite(n) || n < double(std::numeric_limits<T>::lowest()) || n > double(std::numeric_limits<T>::max())) {
            throw std::runtime_error("Engine value is out of range");
        }
        if constexpr (std::is_integral_v<T>) if (std::floor(n) != n) throw std::runtime_error("Expected an integer");
        return static_cast<T>(n);
    }
}
Entity& owner(const Bindings& bindings, Entity* target = nullptr) {
    if (!bindings->owner || !bindings->scene) throw std::runtime_error("Script entity no longer exists");
    if (!target) return *bindings->owner;
    for (Entity& entity : bindings->scene->entities) if (&entity == target) return entity;
    throw std::runtime_error("Referenced entity no longer exists");
}
template<class C> C& component(const Bindings& bindings, Entity* target) {
    auto* result = owner(bindings, target).getComponent<C>();
    if (!result) throw std::runtime_error("Component no longer exists");
    return *result;
}
template<class C, class T> void field(const std::shared_ptr<Object>& object, const Bindings& bindings,
    Entity* target, const char* name, T C::*pointer, bool positive = false) {
    object->property(name, [=] { return pack(component<C>(bindings, target).*pointer); },
        [=](const Value& value) {
            const T next = unpack<T>(value);
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
                if (positive && next <= 0) throw std::runtime_error(std::string(name) + " must be positive");
            } else if constexpr (std::is_same_v<T, Vector3>) {
                if (positive && (next.x <= 0 || next.y <= 0 || next.z <= 0)) throw std::runtime_error(std::string(name) + " dimensions must be positive");
            }
            component<C>(bindings, target).*pointer = next;
        });
}
template<class C> std::shared_ptr<Object> baseComponent(const Bindings& bindings, Entity* target) {
    auto object = std::make_shared<Object>();
    object->property("enabled", [=] { return component<C>(bindings, target).enabled; },
        [=](const Value& value) { component<C>(bindings, target).enabled = value.boolean(); });
    return object;
}
Value componentObject(const Bindings& bindings, Entity* target, std::string name) {
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(dot + 1);
    Entity& entity = owner(bindings, target);
    if (name == "MeshRenderer" && entity.getComponent<MeshRenderer>()) {
        auto object = baseComponent<MeshRenderer>(bindings, target);
        field(object, bindings, target, "color", &MeshRenderer::color);
        field(object, bindings, target, "unlit", &MeshRenderer::unlit); return object;
    }
    if (name == "Camera" && entity.getComponent<Camera>()) {
        auto object = baseComponent<Camera>(bindings, target);
        field(object, bindings, target, "depth", &Camera::depth); field(object, bindings, target, "layers", &Camera::layers);
        field(object, bindings, target, "clearColor", &Camera::clearColor); field(object, bindings, target, "orthographic", &Camera::orthographic);
        field(object, bindings, target, "orthographicSize", &Camera::orthographicSize, true);
        object->property("fieldOfView", [=] { return Value(component<Camera>(bindings, target).fieldOfView); }, [=](const Value& value) {
            const float fov = unpack<float>(value); if (fov <= 0 || fov >= pi) throw std::runtime_error("fieldOfView must be between 0 and pi");
            component<Camera>(bindings, target).fieldOfView = fov;
        });
        field(object, bindings, target, "nearPlane", &Camera::nearPlane, true); field(object, bindings, target, "farPlane", &Camera::farPlane, true); return object;
    }
    if (name == "Rigidbody" && entity.getComponent<Rigidbody>()) {
        auto object = baseComponent<Rigidbody>(bindings, target);
        field(object, bindings, target, "velocity", &Rigidbody::velocity); field(object, bindings, target, "mass", &Rigidbody::mass, true);
        field(object, bindings, target, "useGravity", &Rigidbody::useGravity); field(object, bindings, target, "isKinematic", &Rigidbody::isKinematic); return object;
    }
    if (name == "BoxCollider" && entity.getComponent<BoxCollider>()) {
        auto object = baseComponent<BoxCollider>(bindings, target);
        field(object, bindings, target, "center", &BoxCollider::center); field(object, bindings, target, "size", &BoxCollider::size, true); return object;
    }
    if (name == "SphereCollider" && entity.getComponent<SphereCollider>()) {
        auto object = baseComponent<SphereCollider>(bindings, target);
        field(object, bindings, target, "center", &SphereCollider::center); field(object, bindings, target, "radius", &SphereCollider::radius, true); return object;
    }
    if (name == "CapsuleCollider" && entity.getComponent<CapsuleCollider>()) {
        auto object = baseComponent<CapsuleCollider>(bindings, target);
        field(object, bindings, target, "center", &CapsuleCollider::center); field(object, bindings, target, "radius", &CapsuleCollider::radius, true);
        field(object, bindings, target, "height", &CapsuleCollider::height, true); return object;
    }
    if (name == "Image" && entity.getComponent<Image>()) {
        auto object = baseComponent<Image>(bindings, target);
        field(object, bindings, target, "color", &Image::color); field(object, bindings, target, "width", &Image::width, true);
        field(object, bindings, target, "height", &Image::height, true); return object;
    }
    if (name == "SimpleText" && entity.getComponent<SimpleText>()) {
        auto object = baseComponent<SimpleText>(bindings, target);
        field(object, bindings, target, "text", &SimpleText::text); field(object, bindings, target, "color", &SimpleText::color);
        field(object, bindings, target, "pixelSize", &SimpleText::pixelSize, true); field(object, bindings, target, "centered", &SimpleText::centered); return object;
    }
    if (name == "Button" && entity.getComponent<Button>()) {
        auto object = baseComponent<Button>(bindings, target);
        field(object, bindings, target, "interactable", &Button::interactable);
        field(object, bindings, target, "normalColor", &Button::normalColor); field(object, bindings, target, "hoverColor", &Button::hoverColor);
        field(object, bindings, target, "pressedColor", &Button::pressedColor); field(object, bindings, target, "disabledColor", &Button::disabledColor);
        object->property("hovered", [=] { return component<Button>(bindings, target).hovered; });
        object->property("pressed", [=] { return component<Button>(bindings, target).pressed; });
        object->property("onClick", [] { return Value{}; }, [=](const Value& value) {
            if (value.isNull()) { component<Button>(bindings, target).onClick = {}; return; }
            const auto callback = value.callable();
            component<Button>(bindings, target).onClick = [callback, bindings] {
                if (!bindings->owner) return;
                try { Arguments args; callback->function(args); }
                catch (const std::exception& error) { if (bindings->failed) bindings->failed(error.what()); }
            };
        }); return object;
    }
    if (name == "Canvas" && entity.getComponent<Canvas>()) {
        auto object = baseComponent<Canvas>(bindings, target);
        object->property("camera", [=] {
            auto* camera = component<Canvas>(bindings, target).camera;
            return camera ? entityObject(bindings, camera) : Value{};
        }); return object;
    }
    if (name == "EryScript" && entity.getComponent<EryScript>()) {
        auto object = baseComponent<EryScript>(bindings, target);
        field(object, bindings, target, "code", &EryScript::code); field(object, bindings, target, "runOnAwake", &EryScript::runOnAwake);
        field(object, bindings, target, "runInUpdate", &EryScript::runInUpdate); return object;
    }
    return {}; // Unknown/unattached components behave like GetComponent in Unity.
}
Value transformObject(const Bindings& bindings, Entity* target) {
    auto object = std::make_shared<Object>();
    const auto transform = [=]() -> Transform& { return owner(bindings, target).transform; };
    object->property("localPosition", [=] { return pack(transform().localPosition); }, [=](const Value& v) { transform().localPosition = unpack<Vector3>(v); });
    object->property("position", [=] { return pack(transform().position()); }, [=](const Value& v) { transform().setPosition(unpack<Vector3>(v)); });
    object->property("localRotation", [=] { return pack(transform().localRotation); }, [=](const Value& v) { transform().localRotation = unpack<Quaternion>(v); });
    object->property("rotation", [=] { return pack(transform().rotation()); }, [=](const Value& v) { transform().setRotation(unpack<Quaternion>(v)); });
    object->property("localScale", [=] { return pack(transform().localScale); }, [=](const Value& v) { transform().localScale = unpack<Vector3>(v); });
    object->property("lossyScale", [=] { return pack(transform().lossyScale()); });
    object->property("eulerAngles", [=] { return pack(transform().eulerAngles()); }, [=](const Value& v) { transform().setEulerAngles(unpack<Vector3>(v)); });
    object->property("localEulerAngles", [=] { return pack(transform().localEulerAngles()); }, [=](const Value& v) { transform().setLocalEulerAngles(unpack<Vector3>(v)); });
    object->property("right", [=] { return pack(transform().right()); }); object->property("up", [=] { return pack(transform().up()); });
    object->property("forward", [=] { return pack(transform().forward()); });
    object->fields["Translate"] = Value(Function([=](Arguments& args) {
        args.requireCount(1); transform().setPosition(transform().position() + unpack<Vector3>(args.values[0])); return Value{};
    }));
    return object;
}
Value entityObject(const Bindings& bindings, Entity* target = nullptr) {
    auto object = std::make_shared<Object>();
    object->property("transform", [=] { return transformObject(bindings, target); });
    object->property("tag", [=] { return owner(bindings, target).tag; }, [=](const Value& v) { owner(bindings, target).tag = v.string(); });
    object->property("layer", [=] { return int(owner(bindings, target).layer); }, [=](const Value& v) {
        const auto layer = unpack<unsigned>(v); if (layer > 31) throw std::runtime_error("Layer must be between 0 and 31");
        owner(bindings, target).layer = static_cast<std::uint8_t>(layer);
    });
    object->property("visible", [=] { return owner(bindings, target).visible; }, [=](const Value& v) { owner(bindings, target).visible = v.boolean(); });
    object->property("parent", [=] { const auto parent = owner(bindings, target).parent(); return parent ? entityObject(bindings, parent) : Value{}; });
    object->property("childCount", [=] { return double(owner(bindings, target).childCount()); });
    const Value child(Function([=](Arguments& args) {
        args.requireCount(1); const auto entity = owner(bindings, target).getChild(unpack<unsigned>(args.values[0]));
        return entity ? entityObject(bindings, entity) : Value{};
    }));
    object->fields["getChild"] = child; object->fields["GetChild"] = child;
    object->fields["GetComponent"] = Value(Function([=](Arguments& args) {
        if (args.typeNames.size() == 1) { args.requireCount(0); return componentObject(bindings, target, args.typeNames[0]); }
        args.requireCount(1); return componentObject(bindings, target, args.values[0].string());
    }));
    return object;
}
Vector3 vectorArgs(Arguments& args) {
    if (args.values.size() == 1) return unpack<Vector3>(args.values[0]);
    args.requireCount(3); return {unpack<float>(args.values[0]), unpack<float>(args.values[1]), unpack<float>(args.values[2])};
}
void bindEngine(eryscript::Runtime& runtime, const Bindings& bindings) {
    auto types = std::make_shared<Object>();
    auto vector = std::make_shared<Object>();
    vector->fields["zero"] = pack(Vector3{}); vector->fields["one"] = pack(Vector3{1, 1, 1});
    vector->fields["Length"] = Value(Function([](Arguments& args) { args.requireCount(1); return Value(length(unpack<Vector3>(args.values[0]))); }));
    vector->fields["Normalize"] = Value(Function([](Arguments& args) { args.requireCount(1); return pack(normalized(unpack<Vector3>(args.values[0]))); }));
    auto quaternion = std::make_shared<Object>(); quaternion->fields["identity"] = pack(Quaternion{});
    const Value radians(Function([](Arguments& args) { return pack(Quaternion::fromEuler(vectorArgs(args))); }));
    const Value degrees(Function([](Arguments& args) { return pack(Quaternion::fromEulerDegrees(vectorArgs(args))); }));
    quaternion->fields["FromEuler"] = radians; quaternion->fields["FromEulerDegrees"] = degrees; quaternion->fields["Euler"] = degrees;
    quaternion->fields["ToEuler"] = Value(Function([](Arguments& args) { args.requireCount(1); return pack(unpack<Quaternion>(args.values[0]).toEuler()); }));
    quaternion->fields["ToEulerDegrees"] = Value(Function([](Arguments& args) { args.requireCount(1); return pack(unpack<Quaternion>(args.values[0]).toEulerDegrees()); }));
    const Function newVector = [](Arguments& args) { if (args.values.empty()) return pack(Vector3{}); return pack(vectorArgs(args)); };
    const Function newQuaternion = [](Arguments& args) {
        if (args.values.empty()) return pack(Quaternion{});
        args.requireCount(4); return pack(Quaternion{unpack<float>(args.values[0]), unpack<float>(args.values[1]), unpack<float>(args.values[2]), unpack<float>(args.values[3])});
    };
    const Function newColor = [](Arguments& args) {
        if (args.values.empty()) return pack(Color{});
        args.requireCount(3); return pack(Color{unpack<std::uint8_t>(args.values[0]), unpack<std::uint8_t>(args.values[1]), unpack<std::uint8_t>(args.values[2])});
    };
    runtime.constructor("Vector3", newVector); runtime.constructor("Tiny3D.Vector3", newVector);
    runtime.constructor("Quaternion", newQuaternion); runtime.constructor("Tiny3D.Quaternion", newQuaternion);
    runtime.constructor("Color", newColor); runtime.constructor("Tiny3D.Color", newColor);
    types->fields["Vector3"] = vector; types->fields["Quaternion"] = quaternion;
    runtime.bind("Vector3", vector); runtime.bind("Quaternion", quaternion); runtime.bind("Tiny3D", types);
    auto time = std::make_shared<Object>();
    time->property("deltaTime", [=] { return Value(bindings->deltaTime); }); time->property("time", [=] { return Value(bindings->time); });
    runtime.bind("Time", time);
    auto keys = std::make_shared<Object>();
    static constexpr std::pair<const char*, Key> keyNames[]{
        {"None", Key::None}, {"Cancel", Key::Cancel}, {"Backspace", Key::Backspace},
        {"Tab", Key::Tab}, {"Clear", Key::Clear}, {"Enter", Key::Enter},
        {"Shift", Key::Shift}, {"Control", Key::Control}, {"Alt", Key::Alt},
        {"Pause", Key::Pause}, {"CapsLock", Key::CapsLock}, {"Kana", Key::Kana},
        {"Hangul", Key::Hangul}, {"ImeOn", Key::ImeOn}, {"Junja", Key::Junja},
        {"Final", Key::Final}, {"Hanja", Key::Hanja}, {"Kanji", Key::Kanji},
        {"ImeOff", Key::ImeOff}, {"Escape", Key::Escape}, {"Convert", Key::Convert},
        {"NonConvert", Key::NonConvert}, {"Accept", Key::Accept}, {"ModeChange", Key::ModeChange},
        {"Space", Key::Space}, {"PageUp", Key::PageUp}, {"PageDown", Key::PageDown},
        {"End", Key::End}, {"Home", Key::Home}, {"LeftArrow", Key::LeftArrow},
        {"UpArrow", Key::UpArrow}, {"RightArrow", Key::RightArrow}, {"DownArrow", Key::DownArrow},
        {"Select", Key::Select}, {"Print", Key::Print}, {"Execute", Key::Execute},
        {"PrintScreen", Key::PrintScreen}, {"Insert", Key::Insert}, {"Delete", Key::Delete},
        {"Help", Key::Help}, {"Digit0", Key::Digit0}, {"Digit1", Key::Digit1},
        {"Digit2", Key::Digit2}, {"Digit3", Key::Digit3}, {"Digit4", Key::Digit4},
        {"Digit5", Key::Digit5}, {"Digit6", Key::Digit6}, {"Digit7", Key::Digit7},
        {"Digit8", Key::Digit8}, {"Digit9", Key::Digit9}, {"A", Key::A},
        {"B", Key::B}, {"C", Key::C}, {"D", Key::D},
        {"E", Key::E}, {"F", Key::F}, {"G", Key::G},
        {"H", Key::H}, {"I", Key::I}, {"J", Key::J},
        {"K", Key::K}, {"L", Key::L}, {"M", Key::M},
        {"N", Key::N}, {"O", Key::O}, {"P", Key::P},
        {"Q", Key::Q}, {"R", Key::R}, {"S", Key::S},
        {"T", Key::T}, {"U", Key::U}, {"V", Key::V},
        {"W", Key::W}, {"X", Key::X}, {"Y", Key::Y},
        {"Z", Key::Z}, {"LeftWindows", Key::LeftWindows}, {"RightWindows", Key::RightWindows},
        {"Menu", Key::Menu}, {"Sleep", Key::Sleep}, {"Numpad0", Key::Numpad0},
        {"Numpad1", Key::Numpad1}, {"Numpad2", Key::Numpad2}, {"Numpad3", Key::Numpad3},
        {"Numpad4", Key::Numpad4}, {"Numpad5", Key::Numpad5}, {"Numpad6", Key::Numpad6},
        {"Numpad7", Key::Numpad7}, {"Numpad8", Key::Numpad8}, {"Numpad9", Key::Numpad9},
        {"NumpadMultiply", Key::NumpadMultiply}, {"NumpadAdd", Key::NumpadAdd}, {"NumpadSeparator", Key::NumpadSeparator},
        {"NumpadSubtract", Key::NumpadSubtract}, {"NumpadDecimal", Key::NumpadDecimal}, {"NumpadDivide", Key::NumpadDivide},
        {"F1", Key::F1}, {"F2", Key::F2}, {"F3", Key::F3},
        {"F4", Key::F4}, {"F5", Key::F5}, {"F6", Key::F6},
        {"F7", Key::F7}, {"F8", Key::F8}, {"F9", Key::F9},
        {"F10", Key::F10}, {"F11", Key::F11}, {"F12", Key::F12},
        {"F13", Key::F13}, {"F14", Key::F14}, {"F15", Key::F15},
        {"F16", Key::F16}, {"F17", Key::F17}, {"F18", Key::F18},
        {"F19", Key::F19}, {"F20", Key::F20}, {"F21", Key::F21},
        {"F22", Key::F22}, {"F23", Key::F23}, {"F24", Key::F24},
        {"NumLock", Key::NumLock}, {"ScrollLock", Key::ScrollLock}, {"NumpadEqual", Key::NumpadEqual},
        {"LeftShift", Key::LeftShift}, {"RightShift", Key::RightShift}, {"LeftControl", Key::LeftControl},
        {"RightControl", Key::RightControl}, {"LeftAlt", Key::LeftAlt}, {"RightAlt", Key::RightAlt},
        {"BrowserBack", Key::BrowserBack}, {"BrowserForward", Key::BrowserForward}, {"BrowserRefresh", Key::BrowserRefresh},
        {"BrowserStop", Key::BrowserStop}, {"BrowserSearch", Key::BrowserSearch}, {"BrowserFavorites", Key::BrowserFavorites},
        {"BrowserHome", Key::BrowserHome}, {"VolumeMute", Key::VolumeMute}, {"VolumeDown", Key::VolumeDown},
        {"VolumeUp", Key::VolumeUp}, {"MediaNextTrack", Key::MediaNextTrack}, {"MediaPreviousTrack", Key::MediaPreviousTrack},
        {"MediaStop", Key::MediaStop}, {"MediaPlayPause", Key::MediaPlayPause}, {"LaunchMail", Key::LaunchMail},
        {"LaunchMedia", Key::LaunchMedia}, {"LaunchApp1", Key::LaunchApp1}, {"LaunchApp2", Key::LaunchApp2},
        {"Semicolon", Key::Semicolon}, {"Equal", Key::Equal}, {"Comma", Key::Comma},
        {"Minus", Key::Minus}, {"Period", Key::Period}, {"Slash", Key::Slash},
        {"Backquote", Key::Backquote}, {"LeftBracket", Key::LeftBracket}, {"Backslash", Key::Backslash},
        {"RightBracket", Key::RightBracket}, {"Quote", Key::Quote}, {"Oem8", Key::Oem8},
        {"InternationalBackslash", Key::InternationalBackslash}, {"Process", Key::Process}, {"Packet", Key::Packet},
        {"Attention", Key::Attention}, {"CursorSelect", Key::CursorSelect}, {"ExtendSelect", Key::ExtendSelect},
        {"EraseEndOfFile", Key::EraseEndOfFile}, {"Play", Key::Play}, {"Zoom", Key::Zoom},
        {"Pa1", Key::Pa1}, {"OemClear", Key::OemClear}, {"NumpadEnter", Key::NumpadEnter},
    };
    for (const auto& key : keyNames) keys->fields[key.first] = int(key.second);
    runtime.bind("Key", keys);
    auto buttons = std::make_shared<Object>();
    buttons->fields = {{"Left", int(MouseButton::Left)}, {"Right", int(MouseButton::Right)}, {"Middle", int(MouseButton::Middle)},
        {"Back", int(MouseButton::Back)}, {"Forward", int(MouseButton::Forward)}};
    runtime.bind("MouseButton", buttons);
    auto input = std::make_shared<Object>();
    for (int event = 0; event < 3; ++event) {
        const char* keyMethods[]{"GetKey", "GetKeyDown", "GetKeyUp"};
        input->fields[keyMethods[event]] = Value(Function([=](Arguments& args) {
            args.requireCount(1);
            const Value key = std::holds_alternative<std::string>(args.values[0].data) ? keys->get(args.values[0].string()) : args.values[0];
            const auto code = unpack<unsigned>(key); if (code >= unsigned(Key::Count)) throw std::runtime_error("Invalid key");
            const auto k = static_cast<Key>(code);
            return Value(event == 0 ? bindings->input.held(k) : event == 1 ? bindings->input.pressed(k) : bindings->input.released(k));
        }));
        const char* mouseMethods[]{"GetMouseButton", "GetMouseButtonDown", "GetMouseButtonUp"};
        input->fields[mouseMethods[event]] = Value(Function([=](Arguments& args) {
            args.requireCount(1);
            const Value button = std::holds_alternative<std::string>(args.values[0].data) ? buttons->get(args.values[0].string()) : args.values[0];
            const auto code = unpack<unsigned>(button); if (code >= unsigned(MouseButton::Count)) throw std::runtime_error("Invalid mouse button");
            const auto b = static_cast<MouseButton>(code);
            return Value(event == 0 ? bindings->input.held(b) : event == 1 ? bindings->input.pressed(b) : bindings->input.released(b));
        }));
    }
    input->property("focused", [=] { return Value(bindings->input.focused); });
    input->property("mouseX", [=] { return Value(bindings->input.mouseX); }); input->property("mouseY", [=] { return Value(bindings->input.mouseY); });
    input->property("mouseDeltaX", [=] { return Value(bindings->input.mouseDeltaX); }); input->property("mouseDeltaY", [=] { return Value(bindings->input.mouseDeltaY); });
    input->property("mouseWheel", [=] { return Value(bindings->input.mouseWheel); }); input->property("mouseWheelHorizontal", [=] { return Value(bindings->input.mouseWheelHorizontal); });
    runtime.bind("Input", input);
    runtime.bind("LayerMask", Value(Function([](Arguments& args) { args.requireCount(1); return Value(double(layerMask(unpack<unsigned>(args.values[0])))); })));
    runtime.bind("entity", entityObject(bindings));
    runtime.bind("gameObject", entityObject(bindings));
}
} // namespace

EryScript::~EryScript() {
    if (bindings_) { bindings_->owner = nullptr; bindings_->scene = nullptr; bindings_->failed = {}; }
}
void EryScript::restart() { runtime_.stop(); initialized_ = false; error_.clear(); }
void Scripts::run(Scene& scene, Entity& entity, EryScript& script, float seconds, const Input& input, bool update) {
    if (!script.enabled) return;
    if (!script.bindings_) {
        script.bindings_ = std::make_shared<ScriptBindings>();
        bindEngine(script.runtime_, script.bindings_);
    }
    auto& bindings = *script.bindings_;
    bindings.scene = &scene; bindings.owner = &entity; bindings.deltaTime = seconds;
    bindings.time = time_; bindings.input = input;
    bindings.failed = [&script, state = &bindings](const std::string& error) {
        script.error_ = error; script.runtime_.stop();
        std::cerr << "EryScript [" << state->owner->tag << "]: " << script.error_ << '\n';
    };
    try {
        if (!script.initialized_ || script.loadedSource_ != script.code) {
            script.initialized_ = true; script.loadedSource_ = script.code; script.error_.clear();
            script.runtime_.load(script.code, entityObject(script.bindings_));
            if (script.runOnAwake && script.runtime_.hasMethod("Awake")) script.runtime_.call("Awake");
        }
        if (script.error_.empty() && script.enabled && update && script.runInUpdate && script.runtime_.hasMethod("Update")) script.runtime_.call("Update");
    } catch (const std::exception& error) {
        bindings.failed(error.what());
    }
}
void Scripts::awake(Scene& scene) {
    for (Entity& entity : scene.entities) for (auto& component : entity.components) {
        if (auto* script = dynamic_cast<EryScript*>(component.get())) run(scene, entity, *script, 0, {}, false);
    }
    UI{}.refresh(scene);
}
void Scripts::update(Scene& scene, float seconds, const Input& input) {
    if (!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("Script delta time must be finite and nonnegative");
    time_ += seconds;
    for (Entity& entity : scene.entities) for (auto& component : entity.components) {
        if (auto* script = dynamic_cast<EryScript*>(component.get())) run(scene, entity, *script, seconds, input, true);
    }
    UI{}.refresh(scene); // Script changes to text/images appear in this frame.
}
} // namespace tiny3d
