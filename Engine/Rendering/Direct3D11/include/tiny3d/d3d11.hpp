#pragma once
#include "tiny3d/graphics.hpp"

namespace tiny3d::d3d11 {
struct Options {
    bool softwareDevice = false; // WARP, for deterministic tests without a GPU.
    bool debug = false; // Requires the Windows Graphics Tools debug layer.
};
// Pass a Win32 HWND as void*, or nullptr for offscreen rendering/readback.
std::unique_ptr<RenderBackend> createRenderer(void* nativeWindow, int width, int height, Options options = {});
} // namespace tiny3d::d3d11
