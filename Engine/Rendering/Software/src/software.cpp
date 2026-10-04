#include "tiny3d/engine.hpp"

#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tiny3d {
namespace {
class SoftwareRenderer final : public RenderBackend {
public:
    SoftwareRenderer(void* window, int width, int height) : renderer_(width, height)
#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
        , window_(static_cast<HWND>(window))
#endif
    { (void)window; }
    ~SoftwareRenderer() override {
#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
        if (bitmap_) { SelectObject(dc_, original_); DeleteObject(bitmap_); }
        if (dc_) DeleteDC(dc_);
#endif
    }
    void resize(int w, int h) override { renderer_.resize(w, h); }
    int width() const override { return renderer_.width(); }
    int height() const override { return renderer_.height(); }
    const char* name() const override { return "Software"; }
    void render(const Scene& scene, Color background) override { renderer_.render(scene, background); }
    std::vector<std::uint32_t> readPixels() override { return renderer_.pixels(); }
    void present(int w, int h) override {
#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
        if (!window_ || w <= 0 || h <= 0) return;
        HDC target = GetDC(window_);
        if (!target) return;
        struct Release { HWND window; HDC dc; ~Release() { ReleaseDC(window, dc); } } release{window_, target};
        if (!dc_) dc_ = CreateCompatibleDC(target);
        if (!dc_) return;
        if (clientWidth_ != w || clientHeight_ != h) {
            auto next = CreateCompatibleBitmap(target, w, h);
            if (!next) return;
            auto previous = SelectObject(dc_, next);
            if (!previous || previous == HGDI_ERROR) { DeleteObject(next); return; }
            if (bitmap_) DeleteObject(bitmap_); else original_ = previous;
            bitmap_ = next; clientWidth_ = w; clientHeight_ = h;
        }
        RECT client{0, 0, w, h};
        FillRect(dc_, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const auto viewport = fitViewport(w, h, width(), height());
        if (viewport.width > 0 && viewport.height > 0) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width(); info.bmiHeader.biHeight = -height();
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc_, viewport.x, viewport.y, viewport.width, viewport.height, 0, 0, width(), height(),
                renderer_.pixels().data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }
        BitBlt(target, 0, 0, w, h, dc_, 0, 0, SRCCOPY);
#else
        (void)w; (void)h;
#endif
    }
private:
    Renderer renderer_;
#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
    HWND window_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ original_ = nullptr;
    int clientWidth_ = 0, clientHeight_ = 0;
#endif
};
}
std::unique_ptr<RenderBackend> createSoftwareRenderer(void* window, int width, int height) {
    return std::make_unique<SoftwareRenderer>(window, width, height);
}
} // namespace tiny3d
