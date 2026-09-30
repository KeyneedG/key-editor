#include "tiny3d/engine.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32) && !defined(TINY3D_HEADLESS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace tiny3d {
namespace {
struct WindowState {
    Renderer& renderer;
    bool running = true;
};

void present(HWND window, HDC dc, const Renderer& renderer) {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const int w = client.right, h = client.bottom;
    if (w <= 0 || h <= 0) return;
    const int drawWidth = std::min(w, MulDiv(h, renderer.width(), renderer.height()));
    const int drawHeight = MulDiv(drawWidth, renderer.height(), renderer.width());
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = renderer.width();
    bitmap.bmiHeader.biHeight = -renderer.height(); // Top row first.
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, (w - drawWidth) / 2, (h - drawHeight) / 2, drawWidth, drawHeight,
                 0, 0, renderer.width(), renderer.height(), renderer.pixels().data(),
                 &bitmap, DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<WindowState*>(creation->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (state) present(window, dc, state->renderer);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (state) state->running = false;
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

struct WindowHandle {
    HWND value;
    ~WindowHandle() { if (IsWindow(value)) DestroyWindow(value); }
};
} // namespace

int run(Game& game, int width, int height, unsigned frameLimit) {
    Renderer renderer(width, height);
    WindowState state{renderer};
    HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr const wchar_t* className = L"Tiny3DWindow";
    WNDCLASSW windowClass{};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Cannot register window class");
    }
    RECT bounds{0, 0, width, height};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    WindowHandle window{CreateWindowExW(0, className, L"Tiny3D", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, &state)};
    if (!window.value) throw std::runtime_error("Cannot create window");
    ShowWindow(window.value, SW_SHOW);

    constexpr std::array<int, static_cast<std::size_t>(Key::Count)> keys{
        'W', 'S', 'A', 'D', VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, 'R'
    };
    Input input;
    auto previousTime = std::chrono::steady_clock::now();
    unsigned frames = 0;
    std::string previousTitle;
    while (state.running) {
        const auto frameStart = std::chrono::steady_clock::now();
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) state.running = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!state.running) break;
        input.previous = input.down;
        const bool focused = GetForegroundWindow() == window.value;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            input.down[i] = focused && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        }
        const float elapsed = std::chrono::duration<float>(frameStart - previousTime).count();
        previousTime = frameStart;
        game.update(std::min(elapsed, .05f), input); // Avoid jumps after pauses or window dragging.
        renderer.render(game.scene(), game.camera());
        const std::string title = game.title();
        if (title != previousTitle) {
            SetWindowTextA(window.value, title.c_str());
            previousTitle = title;
        }
        InvalidateRect(window.value, nullptr, FALSE);
        UpdateWindow(window.value);
        if (frameLimit && ++frames >= frameLimit) break;
        std::this_thread::sleep_until(frameStart + std::chrono::milliseconds(16));
    }
    return 0;
}
} // namespace tiny3d

#else
namespace tiny3d {
int run(Game&, int, int, unsigned) {
    std::cerr << "The window backend is unavailable in this build. Use tiny3d_project --render frame.ppm, "
                 "or add a platform backend in src/platform.cpp.\n";
    return 1;
}
} // namespace tiny3d
#endif
