# Tiny3D

A small, extensible C++17 3D engine with no external libraries. The software
renderer uses the standard library; the native window and input use Windows
Win32/GDI. CMake reads JSON without a separate JSON library.

```text
Engine/
  include/tiny3d/    Public math, engine, and project API
  src/              Renderer, window/input loop, and project entry point
  Tiny3D.sln        Visual Studio editor solution
  Tiny3D.vcxproj    Editor project, including C++17 IntelliSense settings
  engine.json       Selected project, mode, and automatic startup
  CMakeLists.txt    Engine library build
  project.cmake     Project manifest loading and executable build
  build.ps1         Visual Studio build wrapper
  launch.ps1        Automatic project startup
  README.md         This guide
DEMO/
  game.cpp          Playable sample
  project.json      Sample source manifest
  README.md         Sample instructions
```

All engine files belong to `Engine`; all sample files belong to `DEMO`.
You can delete `DEMO`. Select your own project or leave `project` empty to build
only `tiny3d.lib`. Generated output stays in `Engine/build` and can be deleted.

## Visual Studio 2022

1. Install **Desktop development with C++**, the MSVC v143 toolset, a Windows SDK,
   and **C++ CMake tools for Windows** through Visual Studio Installer.
2. Open `Engine/Tiny3D.sln`. The solution wraps CMake/Ninja and owns all build
   dependencies; your game's folder needs no solution or project files.
3. Edit `Engine/engine.json` to select a project:

   ```json
   {
     "project": "../MyGame",
     "mode": "editor",
     "autoStart": true
   }
   ```

   Paths are relative to this JSON file or absolute. Use forward slashes or escape
   backslashes. The selected directory must contain `project.json`.
   Both `editor` and `game` currently run the same runtime. Editing tools can be
   added later.
4. Choose **Debug | x64** or **Release | x64** and press **Ctrl+Shift+B**.
   A successful build starts the selected project automatically. Close it before
   rebuilding. The executable is `Engine/build/<configuration>/tiny3d_project.exe`.
5. For debugging, set `autoStart` to `false` and press **F5**. Breakpoints work in
   the engine and selected game.

Each build reads the settings and source manifest again. Rebuild after changing
the selected project or mode. The executable uses your project's directory as
its working directory, so relative asset paths belong to your game.

Engine sources and the demo's `game.cpp` appear together in Solution Explorer
while `DEMO` exists. For your own source files, use **Add > Existing Item** on the
editor project and save with **File > Save All**. This provides an editing context;
`project.json` controls which files compile. The editor sets C++17 for both the
compiler and IntelliSense. Reload the solution after moving it or changing its
project file outside Visual Studio.

See [the demo guide](../DEMO/README.md) to run the included sample.

## PowerShell build

From the repository directory, an ordinary PowerShell window can run:

```powershell
.\Engine\build.ps1 -Configuration Release
.\Engine\build.ps1 -Configuration Debug -NoLaunch
.\Engine\build.ps1 -Configuration Release -ConfigFile C:\Games\MyGame\engine.json
.\Engine\build.ps1 -Configuration Release -Rebuild
.\Engine\build.ps1 -Configuration Release -Clean
```

The script locates Visual Studio and initializes its x64 compiler environment.
If script execution is disabled:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Engine\build.ps1 -Configuration Release
```

## Your own game

A game directory contains C++ source files and `project.json`:

```json
{
  "sources": ["game.cpp"]
}
```

List every C++ translation unit, with paths relative to the project directory.
The engine's `include` directory and your project's root are available to the
compiler automatically.

Include `tiny3d/project.hpp`, derive a class from `tiny3d::Game`, and implement:

- `update(float seconds, const Input&)` for game logic.
- `scene() const` returning your `Scene` by const reference.
- Optionally `title() const` for the window title.

Implement `std::unique_ptr<tiny3d::Game> tiny3d::createGame()` to construct your
game. The engine supplies `main()` through `src/project_main.cpp`.

Own the scene in your game class. Each `Entity` has a transform, visibility flag,
and an owned list of components. `Component` is abstract with a virtual destructor;
derive from it to add your own component types. Use `addComponent<T>(...)` to
construct a component and `getComponent<T>()` to find the first matching component
(or `nullptr`). Components have an `enabled` flag.

`MeshRenderer` combines a shared mesh and its color in one component. `Camera`
is a component too; its position and rotation come from its entity's transform.
The renderer chooses the enabled camera with the highest `depth` on a visible
entity every frame. Negative depths work; ties use the first camera in scene and
component order. Without an eligible camera, the frame contains only the background.
Camera scale does not affect the view.

```cpp
Entity camera;
camera.transform.position = {0, 1, -5};
camera.addComponent<Camera>().depth = 10;
scene.entities.push_back(std::move(camera));

Entity object;
object.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()), Color{255, 195, 60});
scene.entities.push_back(std::move(object));
```

Entities own their components through `unique_ptr`; move them into the scene
with `std::move` rather than copying. Mesh data can be shared by multiple
`MeshRenderer` components. Entity transforms scale, rotate, then translate.
The engine updates the game and calls `Renderer::render(scene)` every frame.
Update your custom component behavior from `Game::update`. Input exposes
`held(Key)` and `pressed(Key)`.

For custom meshes, fill `vertices` and `triangles` with zero-based vertex indices.
Faces wind counterclockwise when seen from outside. Coordinates use +Y up and +Z
forward; Euler angles are radians, applied X, Y, then Z. Colors are solid with
flat lighting. The renderer handles perspective, six-plane clipping, back-face
culling, and perspective-correct depth testing.

## Direct CMake build

From **Developer PowerShell for VS 2022**:

```powershell
cmake -S Engine -B Engine/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build Engine/build/Release
```

Without `TINY3D_CONFIG`, this builds the engine library only. To select a game,
add `-DTINY3D_CONFIG=<absolute-settings-path>` to the configuration command.
Automatic startup can be disabled with `-DTINY3D_AUTO_START=OFF`.

Other CMake programs can use `add_subdirectory` with the `Engine` directory and
link against `tiny3d`. An independent executable can provide its own `main()` and
call `tiny3d::run(game)`.

The core also builds on Linux and macOS with CMake 3.19+ and a C++17 compiler.
Use `-DTINY3D_HEADLESS=ON` to omit the native window backend. The Visual Studio
wrapper and automatic startup are Windows features.

Managed executables accept `--render <file.ppm>` for a headless RGB frame and
`--frames <count>` for a bounded native-window run.
