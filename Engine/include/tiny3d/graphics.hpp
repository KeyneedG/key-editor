#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tiny3d {
struct Scene;
struct Color;

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
    // Explicit readback for screenshots/tests, never needed by the window loop.
    virtual std::vector<std::uint32_t> readPixels() = 0;
};

namespace graphics {
using BackendFactory = std::function<std::unique_ptr<RenderBackend>(void* nativeWindow, int width, int height)>;
}
std::unique_ptr<RenderBackend> createSoftwareRenderer(void* nativeWindow, int width, int height);
} // namespace tiny3d
