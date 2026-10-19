#pragma once
#include "tiny3d/behaviour.hpp"

// Gameplay state exposed to MenuUI.ery; the script owns the menu and its callbacks.
class CollectorController final : public tiny3d::MonoBehaviour {
public:
    void Awake() override;
    void Update(float seconds, const tiny3d::Input& input) override;
    void setPaused(bool paused);
    bool playing() const { return !paused_; }
    bool quitting() const { return quitRequested; }
    tiny3d::ScreenSettings screenSettings() const { return screen; }
    std::string progress() const;
    tiny3d::ScreenSettings screen{800, 500, false};
    bool quitRequested = false;
private:
    void resetCollectibles();
    std::size_t collected_ = 0;
    bool paused_ = false;
};
