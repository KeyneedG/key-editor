# Tiny3D

A small C++17 3D game engine and a playable collect-the-cubes demo. No third-party
libraries, packages, downloads, or assets. The core uses only the C++ standard
library. The Windows window uses the operating system's built-in Win32/GDI APIs.

The engine has perspective projection, six-plane frustum clipping, back-face
culling, a depth buffer, flat directional lighting, shared triangle meshes,
entity transforms, a camera, keyboard input, and a frame loop.

## Build and run

Use CMake and a C++17 compiler. On Windows, Visual Studio with the **Desktop
development with C++** workload is sufficient. Run these commands in the project
directory (a Developer PowerShell/Command Prompt also works):

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Windows with the Visual Studio generator:

```powershell
.\build\Release\demo.exe
```

With a single-configuration generator such as Makefiles or Ninja, the executable
is `build/demo` (or `build/demo.exe`). Choose release optimization at configuration
time with `-DCMAKE_BUILD_TYPE=Release`.

Move with **WASD**, look with the **arrow keys**, restart with **R**, and exit with
**Escape** or the window close button. Walk up to the five spinning gold cubes to
collect them. Progress appears in the window title. The blue blocks are scenery;
the demo has no physics or solid-object collision.

The renderer and image output also work on Linux and macOS. Those platforms do
not have a window backend yet. Render a frame without a window on any platform:

```sh
build/demo --render frame.ppm
```

On Visual Studio builds, use `build/Release/demo.exe` instead. PPM is a simple
binary RGB image format. To build without any native window API, configure with
`-DTINY3D_HEADLESS=ON`. `demo --frames 3` runs a brief Windows window smoke test
and exits automatically; CTest includes this when the window backend is enabled.

## Extend it

There are three engine files to understand:

- `include/tiny3d/math.hpp`: vectors and scale/rotate/translate operations.
- `include/tiny3d/engine.hpp`: the public API and plain scene data.
- `src/renderer.cpp`: mesh creation, clipping, lighting, and triangle rasterization.

`src/platform.cpp` owns the window, input polling, and timing. `examples/demo.cpp`
shows how to implement a `Game`. The renderer is independent of the window.

Create your own game by subclassing `tiny3d::Game`, owning a `Scene` and `Camera`,
and implementing `update`, `scene`, and `camera`. Start it with `tiny3d::run(game)`.
Add entities directly to `scene.entities`; multiple entities can share one mesh.
Here is a minimal rotating-cube game:

```cpp
#include "tiny3d/engine.hpp"

class MyGame : public tiny3d::Game {
    tiny3d::Scene scene_;
    tiny3d::Camera camera_;
public:
    MyGame() {
        camera_.position = {0, 0, -3};
        scene_.entities.push_back({std::make_shared<tiny3d::Mesh>(tiny3d::cube())});
    }
    void update(float seconds, const tiny3d::Input&) override {
        scene_.entities[0].transform.rotation.y += seconds;
    }
    const tiny3d::Scene& scene() const override { return scene_; }
    const tiny3d::Camera& camera() const override { return camera_; }
};

int main() { MyGame game; return tiny3d::run(game); }
```

Link your executable against the `tiny3d` CMake target. To add another platform,
implement `run` in `src/platform.cpp` and display `Renderer::pixels()`. To add
custom geometry, fill `Mesh::vertices` and `Mesh::triangles` with zero-based vertex
indices. Triangle winding is counterclockwise when viewed from outside.

Coordinates use +Y up and +Z forward. Angles are radians; rotations apply X, Y,
then Z. Transforms scale, rotate, then translate. Lighting is flat per triangle,
and entities have one solid color. Pixels are packed as `0x00RRGGBB`. Rendering
uses a fixed resolution; resizing the window preserves the image's aspect ratio.

There is deliberately no ECS, editor, asset loader, physics, audio, textures,
or plugin system. Add features where you need them, starting with the existing
plain data structures and game update callback.
