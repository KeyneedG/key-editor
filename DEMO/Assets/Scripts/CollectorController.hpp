#pragma once
#include "tiny3d/behaviour.hpp"

// Binds saved menu controls and manages collectible state; scene geometry lives in JSON.
class CollectorController final : public tiny3d::MonoBehaviour {
public:
    void Awake() override;
    void Update(float seconds, const tiny3d::Input& input) override;
    bool playing() const { return mode_ == Mode::Playing; }
    bool quitting() const { return quit_; }
    tiny3d::ScreenSettings screenSettings() const { return screen_; }
    std::string progress() const;
private:
    enum class Mode { Playing, Pause, Settings };
    tiny3d::Entity* find(const std::string& tag) const;
    void showMenu(Mode mode);
    void refreshSettingsLabels();
    void resetCollectibles();
    Mode mode_ = Mode::Playing;
    tiny3d::ScreenSettings screen_{800, 500, false}, pending_{800, 500, false};
    std::size_t collected_ = 0;
    bool quit_ = false;
};
