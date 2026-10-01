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
    Input input;
    bool running = true;
    bool mouseCaptured = false;
    int cursorHideCalls = 0;
    bool hasAbsoluteMousePosition = false;
    POINT absoluteMousePosition{};
};

struct ScreenState {
    ScreenSettings requested{};
    RECT windowedBounds{};
    bool initialized = false, fullscreen = false;
};

void applyScreenSettings(HWND window, Renderer& renderer, ScreenState& state, ScreenSettings settings) {
    if (state.initialized && settings == state.requested) return;
    if (settings.width < 0 || settings.height < 0) throw std::invalid_argument("Screen dimensions cannot be negative");
    const int width = settings.width ? settings.width : renderer.width();
    const int height = settings.height ? settings.height : renderer.height();
    RECT bounds{};
    GetWindowRect(window, &bounds);
    if (!state.fullscreen) state.windowedBounds = bounds;
    if (settings.fullscreen) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            throw std::runtime_error("Cannot get fullscreen monitor bounds");
        }
        bounds = monitor.rcMonitor;
    } else {
        RECT client{0, 0, width, height};
        if (!AdjustWindowRect(&client, WS_OVERLAPPEDWINDOW, FALSE)) throw std::runtime_error("Cannot size window");
        const int x = state.windowedBounds.left, y = state.windowedBounds.top;
        bounds = {x, y, x + client.right - client.left, y + client.bottom - client.top};
    }
    const LONG_PTR style = (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) |
        (settings.fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW);
    SetLastError(0);
    if (!SetWindowLongPtrW(window, GWL_STYLE, style) && GetLastError() != 0) {
        throw std::runtime_error("Cannot change window mode");
    }
    if (!SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, SWP_NOZORDER | SWP_FRAMECHANGED)) {
        throw std::runtime_error("Cannot apply screen settings");
    }
    if (renderer.width() != width || renderer.height() != height) renderer.resize(width, height);
    state.requested = settings;
    state.fullscreen = settings.fullscreen;
    state.initialized = true;
}

void captureMouse(HWND window, WindowState& state, bool capture) {
    capture = capture && state.input.focused && !IsIconic(window);
    if (capture) {
        RECT bounds{};
        GetClientRect(window, &bounds);
        if (bounds.right <= 0 || bounds.bottom <= 0) capture = false;
        else {
            // Raw input provides movement; the hidden system cursor stays away from borders.
            POINT center{bounds.right / 2, bounds.bottom / 2};
            ClientToScreen(window, &center);
            bounds = {center.x, center.y, center.x + 1, center.y + 1};
            capture = ClipCursor(&bounds) != FALSE;
            if (capture && GetCapture() != window) SetCapture(window);
        }
    }
    if (capture == state.mouseCaptured) return;
    state.mouseCaptured = capture;
    state.hasAbsoluteMousePosition = false;
    state.input.mouseDeltaX = state.input.mouseDeltaY = 0;
    if (capture) {
        do { ++state.cursorHideCalls; } while (ShowCursor(FALSE) >= 0);
    } else {
        ClipCursor(nullptr);
        if (GetCapture() == window) ReleaseCapture();
        while (state.cursorHideCalls > 0) { ShowCursor(TRUE); --state.cursorHideCalls; }
    }
}

void keyboardEvent(Input& input, WPARAM value, LPARAM flags, bool down) {
    UINT key = static_cast<UINT>(value);
    const bool extended = (flags & (LPARAM(1) << 24)) != 0;
    if (key == VK_SHIFT) key = MapVirtualKeyW(static_cast<UINT>((flags >> 16) & 0xff), MAPVK_VSC_TO_VK_EX);
    else if (key == VK_CONTROL) key = extended ? VK_RCONTROL : VK_LCONTROL;
    else if (key == VK_MENU) key = extended ? VK_RMENU : VK_LMENU;
    else if (key == VK_RETURN && extended) key = static_cast<UINT>(Key::NumpadEnter);
    // Windows can send PrintScreen only as a key-up message.
    if (key == VK_SNAPSHOT && !down && !input.held(Key::PrintScreen)) input.set(Key::PrintScreen, true);
    if (key > 0 && key < static_cast<UINT>(Key::Count)) input.set(static_cast<Key>(key), down);
    input.set(Key::Shift, input.held(Key::LeftShift) || input.held(Key::RightShift));
    input.set(Key::Control, input.held(Key::LeftControl) || input.held(Key::RightControl));
    input.set(Key::Alt, input.held(Key::LeftAlt) || input.held(Key::RightAlt));
}

void commandEvent(Input& input, int command) {
    if (!input.focused || command < APPCOMMAND_BROWSER_BACKWARD || command > APPCOMMAND_LAUNCH_APP2) return;
    const auto key = static_cast<Key>(static_cast<int>(Key::BrowserBack) + command - APPCOMMAND_BROWSER_BACKWARD);
    input.set(key, true);
    input.set(key, false); // Consumer keys can arrive as commands rather than held keys.
}

void mouseEvent(WindowState& state, const RAWMOUSE& mouse) {
    Input& input = state.input;
    if (!input.focused) return;
    if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
        const bool desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        const int width = GetSystemMetrics(desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        const int height = GetSystemMetrics(desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
        POINT position{MulDiv(mouse.lLastX, width, 65535), MulDiv(mouse.lLastY, height, 65535)};
        if (desktop) {
            position.x += GetSystemMetrics(SM_XVIRTUALSCREEN);
            position.y += GetSystemMetrics(SM_YVIRTUALSCREEN);
        }
        if (state.hasAbsoluteMousePosition) {
            input.mouseDeltaX += static_cast<float>(position.x - state.absoluteMousePosition.x);
            input.mouseDeltaY += static_cast<float>(position.y - state.absoluteMousePosition.y);
        }
        state.absoluteMousePosition = position;
        state.hasAbsoluteMousePosition = true;
    } else {
        input.mouseDeltaX += static_cast<float>(mouse.lLastX);
        input.mouseDeltaY += static_cast<float>(mouse.lLastY);
        state.hasAbsoluteMousePosition = false;
    }
    for (std::size_t i = 0; i < input.mouseButtons.size(); ++i) {
        if (mouse.usButtonFlags & (1u << (i * 2))) input.set(static_cast<MouseButton>(i), true);
        if (mouse.usButtonFlags & (2u << (i * 2))) input.set(static_cast<MouseButton>(i), false);
    }
    const float wheel = static_cast<float>(static_cast<short>(mouse.usButtonData)) / WHEEL_DELTA;
    if (mouse.usButtonFlags & RI_MOUSE_WHEEL) input.mouseWheel += wheel;
    if (mouse.usButtonFlags & RI_MOUSE_HWHEEL) input.mouseWheelHorizontal += wheel;
}

void present(HWND window, HDC dc, const Renderer& renderer) {
    RECT client{};
    GetClientRect(window, &client);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const Viewport viewport = fitViewport(client.right, client.bottom, renderer.width(), renderer.height());
    if (viewport.width <= 0 || viewport.height <= 0) return;
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = renderer.width();
    bitmap.bmiHeader.biHeight = -renderer.height(); // Top row first.
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, viewport.x, viewport.y, viewport.width, viewport.height,
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
    if (!state) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_NCHITTEST:
        if (state->mouseCaptured) return HTCLIENT;
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_SETCURSOR:
        if (state->mouseCaptured) { SetCursor(nullptr); return TRUE; }
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN: case WM_NCRBUTTONDBLCLK:
        if (state->mouseCaptured) return 0;
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        present(window, dc, state->renderer);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SETFOCUS:
        state->input.focused = true;
        state->hasAbsoluteMousePosition = false;
        return 0;
    case WM_KILLFOCUS:
        state->input.releaseAll();
        state->hasAbsoluteMousePosition = false;
        captureMouse(window, *state, false);
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_KEYDOWN: case WM_KEYUP:
        if (state->input.focused) keyboardEvent(state->input, wparam, lparam, message == WM_KEYDOWN);
        return 0;
    case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        if (state->input.focused) keyboardEvent(state->input, wparam, lparam, message == WM_SYSKEYDOWN);
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_APPCOMMAND:
        commandEvent(state->input, GET_APPCOMMAND_LPARAM(lparam));
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_INPUT: {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) != UINT(-1) && raw.header.dwType == RIM_TYPEMOUSE) {
            mouseEvent(*state, raw.data.mouse);
        }
        return DefWindowProcW(window, message, wparam, lparam); // Releases the raw input packet.
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
        SetCapture(window); // Continue receiving button releases during a drag.
        return message == WM_XBUTTONDOWN ? TRUE : 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
        if (!state->mouseCaptured && !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2))) ReleaseCapture();
        return message == WM_XBUTTONUP ? TRUE : 0;
    case WM_ENTERMENULOOP:
        captureMouse(window, *state, false);
        return 0;
    case WM_MOVE: case WM_SIZE:
        if (state->mouseCaptured) captureMouse(window, *state, true);
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        captureMouse(window, *state, false);
        state->running = false;
        return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

struct WindowHandle {
    HWND value;
    bool rawMouse = false;
    ~WindowHandle() {
        if (rawMouse) {
            RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
            RegisterRawInputDevices(&device, 1, sizeof(device));
        }
        if (IsWindow(value)) DestroyWindow(value);
    }
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
    RAWINPUTDEVICE mouse{0x01, 0x02, 0, window.value};
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) {
        throw std::runtime_error("Cannot register mouse input");
    }
    window.rawMouse = true;
    ScreenState screen;
    applyScreenSettings(window.value, renderer, screen, game.screenSettings());
    ShowWindow(window.value, SW_SHOW);

    constexpr std::array<int, 5> mouseButtons{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    Input& input = state.input;
    auto previousTime = std::chrono::steady_clock::now();
    unsigned frames = 0;
    std::string previousTitle;
    while (state.running) {
        const auto frameStart = std::chrono::steady_clock::now();
        input.beginFrame();
        const bool focused = GetForegroundWindow() == window.value && GetFocus() == window.value;
        if (!focused && input.focused) input.releaseAll();
        input.focused = focused;
        captureMouse(window.value, state, game.captureMouse());
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) state.running = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!state.running) break;
        RECT client{};
        GetClientRect(window.value, &client);
        input.renderWidth = renderer.width();
        input.renderHeight = renderer.height();
        input.viewport = fitViewport(client.right, client.bottom, renderer.width(), renderer.height());
        if (input.focused) {
            for (int key = 0; key < 256; ++key) {
                // Events distinguish Enter keys and handle PrintScreen's key-up-only pulse.
                if ((key < VK_BACK && key != VK_CANCEL) || key == VK_RETURN || key == VK_SNAPSHOT) continue;
                input.set(static_cast<Key>(key), (GetAsyncKeyState(key) & 0x8000) != 0);
            }
            for (std::size_t i = 0; i < mouseButtons.size(); ++i) {
                input.set(static_cast<MouseButton>(i), (GetAsyncKeyState(mouseButtons[i]) & 0x8000) != 0);
            }
            POINT position{};
            if (GetCursorPos(&position) && ScreenToClient(window.value, &position)) {
                input.mouseX = position.x;
                input.mouseY = position.y;
            }
        }
        const float elapsed = std::chrono::duration<float>(frameStart - previousTime).count();
        previousTime = frameStart;
        game.update(std::min(elapsed, .05f), input); // Avoid jumps after pauses or window dragging.
        if (game.shouldQuit()) break;
        applyScreenSettings(window.value, renderer, screen, game.screenSettings());
        captureMouse(window.value, state, game.captureMouse());
        renderer.render(game.scene());
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
