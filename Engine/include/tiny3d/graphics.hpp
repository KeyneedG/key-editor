#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tiny3d {
struct Scene;
struct Color;

namespace graphics {
// Optional UI geometry hints; the ordinary mesh remains the software/serialization fallback.
struct QuadInstance {
    float x = 0, y = 0, z = 0, width = 0, height = 0;
    std::uint32_t inkLow = 0, inkHigh = 0; // 35 font bits; high bit marks a glyph.
};
struct ClipPlane { float x = 0, y = 0, z = 0, w = 0; };
struct RenderStats {
    // Actual draw batches and submitted geometry; presentation is excluded.
    std::uint64_t batches = 0, triangles = 0, vertices = 0, cameras = 0, meshUploads = 0;
    std::uint64_t meshSubmissions = 0, instances = 0, instancedBatches = 0, glyphInstances = 0;
};
struct FrameStats {
    RenderStats rendering;
    std::string renderer;
    int width = 0, height = 0;
    // Frame interval includes pacing/vsync and is never clamped to simulation time.
    float frameSeconds = 0, updateSeconds = 0, renderSeconds = 0, presentSeconds = 0;
};
}

// A small rendering boundary. Native handles and API resources stay in the backend.
// Dimensions are the requested rendering resolution, independent of the window size.
class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    virtual void resize(int width, int height) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void render(const Scene& scene, Color background) = 0;
    virtual void present(int clientWidth, int clientHeight) = 0;
    virtual const char* name() const = 0;
    virtual graphics::RenderStats stats() const { return {}; }
    // Explicit readback for screenshots/tests, never needed by the window loop.
    virtual std::vector<std::uint32_t> readPixels() = 0;
};

namespace graphics {
using BackendFactory = std::function<std::unique_ptr<RenderBackend>(void* nativeWindow, int width, int height)>;
}
std::unique_ptr<RenderBackend> createSoftwareRenderer(void* nativeWindow, int width, int height);
} // namespace tiny3d
