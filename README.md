# Tiny3D

A small C++17 3D engine. The renderer uses the C++ standard library; the native
window uses built-in Windows Win32/GDI APIs. There are no third-party runtime
dependencies. JSON is read by [CMake's built-in JSON support](https://cmake.org/cmake/help/v3.19/command/string.html#json),
so Newtonsoft is not needed.

## Layout

```text
include/tiny3d/       Public math, engine, and project API
src/                 Renderer, window/input loop, and project entry point
Editor/              The only source-controlled Visual Studio solution and build tools
tests/               Engine tests
DEMO/                All sample games, sample configurations, and demo checks
```

You can delete `DEMO` completely. The engine does not depend on it. `Editor/engine.json`
starts with no selected project; set `project` to your own game directory. An empty
project builds only `tiny3d.lib`.

## Visual Studio 2022: build and automatically run

1. In Visual Studio Installer, install **Desktop development with C++**, including
   the MSVC v143 toolset, a Windows SDK, and **C++ CMake tools for Windows**.
2. Open `Editor/Tiny3D.sln`. This solution uses Visual Studio's
   [Makefile project support](https://learn.microsoft.com/en-us/cpp/build/reference/creating-a-makefile-project?view=msvc-170)
   to wrap CMake/Ninja;
   it does not generate a solution or project file in your game's source folder.
3. Open `Editor/engine.json` and select your project:

   ```json
   {
     "project": "../MyGame",
     "mode": "editor",
     "autoStart": true
   }
   ```

   `project` is relative to the settings file, or an absolute path. Use forward
   slashes, or escape backslashes in JSON. The directory must contain `project.json`.
   `mode` must be `editor` or `game`. Both modes run the same game runtime for now;
   `editor` is reserved for future editing tools. There is currently no scene editor.
4. Choose **Debug | x64** or **Release | x64** in the toolbar.
5. Use **Build > Build Solution** (`Ctrl+Shift+B`). This compiles the engine and
   selected project's sources into `Editor/build/<configuration>/tiny3d_project.exe`,
   then starts that project automatically. Its working directory is the project
   directory, so relative asset paths belong to your game. A failed project build
   does not launch it. Close a running project before starting another build.

Each build reads the settings and source manifest again. Change `project` or `mode`
and rebuild to switch. No game `.sln`, `.vcxproj`, or CMake files are required.

Edit the engine through the solution's `include` and `src` files. Open game source
files through **File > Open > File**, or add them to the editor project with
**Add > Existing Item** for convenient editing. The JSON manifest controls which
game files actually compile.

For debugging, set `autoStart` to `false`, select **Debug | x64**, and press **F5**.
Visual Studio builds and launches `tiny3d_project.exe` under its C++ debugger. Keeping
automatic startup disabled here avoids launching a second copy during the F5 build.
Breakpoints work in both the engine and the selected game's C++ sources.

## Build from PowerShell

The build script locates Visual Studio 2022 and sets up its x64 compiler tools, so
an ordinary PowerShell window works. Run from the engine's root directory:

```powershell
# Uses Editor/engine.json; launches automatically when a project is selected.
.\Editor\build.ps1 -Configuration Release

# Build without launching, or use another settings file.
.\Editor\build.ps1 -Configuration Debug -NoLaunch
.\Editor\build.ps1 -Configuration Release -ConfigFile C:\Games\MyGame\engine.json

# Rebuild or clean the selected configuration.
.\Editor\build.ps1 -Configuration Release -Rebuild
.\Editor\build.ps1 -Configuration Release -Clean
```

If script execution is disabled, invoke it as Visual Studio does:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Editor\build.ps1 -Configuration Release
```

## Create your own game

Your project is a directory containing C++ source files and this `project.json`:

```json
{
  "sources": ["game.cpp", "player.cpp"]
}
```

List actual C++ translation units; omit `player.cpp` if you do not have that file.
Paths in this list are relative to the project directory. The compiler can include
the project's root and the engine's `include` directory automatically.

Include `tiny3d/project.hpp`, derive a class from `tiny3d::Game`, and implement:

- `update(float seconds, const Input&)`: game logic and input handling.
- `scene() const`: return your scene by const reference.
- `camera() const`: return your camera by const reference.
- Optionally `title() const`: text for the native window title.

Implement `std::unique_ptr<tiny3d::Game> tiny3d::createGame()` to construct your game.
Do not define `main()` in a managed project: `src/project_main.cpp` supplies it.
The minimal implementation is in [DEMO/minimal/game.cpp](DEMO/minimal/game.cpp).

Own your scene and camera in your game class. Add `Entity` values to `scene.entities`;
entities can share one `Mesh`. A transform scales, rotates, then translates vertices.
The engine calls `update` every frame, then renders the returned scene and camera.
Input offers `held(Key)` and `pressed(Key)` for continuous and first-press actions.

For custom geometry, fill `Mesh::vertices` and `Mesh::triangles` with zero-based
vertex indices. Faces wind counterclockwise when seen from outside. Coordinates
use +Y up and +Z forward; Euler angles are radians, applied X, Y, then Z. Entity
colors are solid; lighting is flat per triangle. The renderer handles perspective,
six-plane clipping, back-face culling, and perspective-correct depth testing.

## Engine library and tests

With an empty `project` in `Editor/engine.json`, building the editor solution or
running `Editor/build.ps1` produces the library without any game. To use the
renderer in another CMake program, add this engine directory with `add_subdirectory`
and link your target against `tiny3d`. An independent executable can supply its
own `main()` and call `tiny3d::run(game)`.

For a direct Ninja build and the engine tests, use **Developer PowerShell for
VS 2022** from Visual Studio's **Tools > Command Line** menu:

```powershell
cmake -S . -B Editor/build/Tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Editor/build/Tests
ctest --test-dir Editor/build/Tests --output-on-failure
```

To build a selected project directly, add `"-DTINY3D_CONFIG=C:/Games/MyGame/engine.json"`
to the configuration command. This also enables automatic startup unless its JSON
has `autoStart: false` or you pass `-DTINY3D_AUTO_START=OFF`.

The core and headless image output also build on Linux and macOS with CMake 3.19+
and a C++17 compiler. Use `-DTINY3D_HEADLESS=ON` to omit the native window backend.
The Visual Studio wrapper and automatic startup are Windows features.

The selected executable accepts `--render <file.ppm>` for a single headless RGB
frame and `--frames <count>` for a bounded native-window run. These are engine
utilities available to every managed project. See [DEMO/README.md](DEMO/README.md)
for sample-specific instructions.
