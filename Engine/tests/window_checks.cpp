#include "tiny3d/engine.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <stdexcept>

using namespace tiny3d;
void check(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
class ResizeBackend final : public RenderBackend {
public:
    ResizeBackend(int width, int height) : width_(width), height_(height) {}
    void resize(int width, int height) override { check(width > 0 && height > 0, "Minimize resized the renderer to zero"); width_ = width; height_ = height; ++resizes; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    void render(const Scene&, Color) override {}
    void present(int, int) override {}
    const char* name() const override { return "Window resize test"; }
    std::vector<std::uint32_t> readPixels() override { return {}; }
    int resizes = 0;
private:
    int width_, height_;
};
class ResizeGame final : public Game {
public:
    HWND window = nullptr;
    ResizeBackend* backend = nullptr;
    void update(float, const Input& input) override {
        RECT client{}; GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0) {
            check(input.renderWidth == client.right && input.renderHeight == client.bottom, "Input retained the old render size after window resize");
            check(input.viewport.x == 0 && input.viewport.y == 0 && input.viewport.width == client.right && input.viewport.height == client.bottom,
                "Manual resize left a letterboxed viewport");
        }
        if (frame == 4) settings_ = {640,360,false};
        if (frame == 5) settings_.fullscreen = true;
        if (frame == 7) settings_ = {500,300,false};
    }
    void frameRendered(const graphics::FrameStats& stats) override {
        RECT client{}; GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0) check(stats.width == client.right && stats.height == client.bottom, "Rendering resolution did not follow actual client size");
        if (frame == 0) { resizeClient(450,300); }
        if (frame == 1) { check(stats.width == 450 && stats.height == 300, "ScreenSettings undid the user's manual resize"); resizeClient(300,600); }
        if (frame == 2) { check(stats.width == 300 && stats.height == 600, "Portrait resize was ignored"); ShowWindow(window, SW_MINIMIZE); }
        if (frame == 3) ShowWindow(window, SW_RESTORE);
        if (frame == 4) check(stats.width == 640 && stats.height == 360, "Requested screen settings no longer resize the window");
        if (frame == 5) {
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor); GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
            check(stats.width == monitor.rcMonitor.right - monitor.rcMonitor.left && stats.height == monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                "Fullscreen rendering did not adapt to monitor size");
        }
        if (frame == 7) check(stats.width == 500 && stats.height == 300, "Returning to windowed mode did not resize rendering");
        ++frame;
    }
    Scene& scene() override { return scene_; }
    const Scene& scene() const override { return scene_; }
    ScreenSettings screenSettings() const override { return settings_; }
    bool executeScripts() const override { return false; }
    int frame = 0;
private:
    void resizeClient(int width, int height) {
        RECT bounds{0,0,width,height}; AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
        check(SetWindowPos(window, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER), "Test could not resize its own window");
    }
    Scene scene_;
    ScreenSettings settings_{320,200,false};
};
int main() {
    try {
        ResizeGame game;
        check(run(game,320,200,9,[&](void* window,int width,int height) {
            game.window = static_cast<HWND>(window); auto backend = std::make_unique<ResizeBackend>(width,height); game.backend = backend.get(); return backend;
        }) == 0 && game.frame == 9, "Native resize test did not finish");
        std::cout << "Native window resize, input viewport, minimize, fullscreen, and restore checks passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
