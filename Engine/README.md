# Tiny3D

A small, extensible C++17 3D engine with no external libraries. The software
renderer uses the standard library; the native window and input use Windows
Win32/GDI. CMake reads JSON without a separate JSON library.

```text
Engine/
  include/tiny3d/    Public math, engine, physics, and project API
  src/              Renderer, physics, window/input loop, and project entry point
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
- Optionally `captureMouse() const` to hide and confine the cursor while focused.
- Optionally `shouldQuit() const` to end the game loop.

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
camera.transform.localPosition = {0, 1, -5};
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
`held`, `pressed`, and `released` for both keyboard keys and mouse buttons.

## Transforms and parents

`Vector3` replaces `Vec3`; the old name is retained as an alias. Transform rotations
are `Quaternion` values. Local fields are `localPosition`, `localRotation`, and
`localScale`. World values are calculated through `position()`, `rotation()`, and
`lossyScale()`, so changing a parent immediately affects its descendants. Use
`setPosition(...)` and `setRotation(...)` to write world values. `lossyScale()`
reports world axis lengths; rotation combined with nonuniform parent scaling can
produce shear, which cannot be represented by a rotation and three scale values.
`point(...)`, `inversePoint(...)`, and `vector(...)` convert through the whole
hierarchy. `right()`, `up()`, and `forward()` return world rotation directions.

Use `entity.setParent(&parent)` and `entity.parent()`. Parenting preserves world
position, rotation, and scale magnitudes by default. Pass `false` to keep local
values instead. `setParent(nullptr)` detaches; cycles are rejected. Add entities
to `scene.entities` before linking them. This container is a `deque`, so appending
does not invalidate entity references or parent pointers. Keep parents alive;
detach their children before removing or moving linked entities.

```cpp
Entity& player = scene.entities.emplace_back();
player.transform.localPosition = {0, 0, -5};
Entity& camera = scene.entities.emplace_back();
camera.addComponent<Camera>();
camera.setParent(&player, false);
camera.transform.localPosition = {0, 1.5f, 0};

player.transform.localRotation = Quaternion::fromEuler({0, yaw, 0});
camera.transform.localRotation = Quaternion::fromEuler({pitch, 0, 0});
Vector3 angles = camera.transform.rotation().toEuler();
```

Euler vectors are angles, not direction vectors. `Quaternion::fromEuler(Vector3)`
and `toEuler()` use radians, applied X, then Y, then Z. For degrees, use
`fromEulerDegrees(...)` and `toEulerDegrees()`. `axisAngle(axis, radians)`,
quaternion multiplication, `rotate(vector, quaternion)`, and `inverse(quaternion)`
are also available. Quaternion multiplication applies the right operand first.
Transforms also offer `eulerAngles()`, `localEulerAngles()`, `setEulerAngles(...)`,
and `setLocalEulerAngles(...)`, all in radians. Euler representations are not unique.

## Keyboard and mouse input

Use key names such as `Key::W`, `Key::A`, `Key::LeftArrow`, `Key::Space`, and
`Key::Escape`. The enum includes A-Z, `Digit0`-`Digit9`, navigation and punctuation
keys, F1-F24, numpad keys, lock keys, left/right modifiers, Windows/menu keys,
browser/media/volume keys, and IME keys. `Key::NumpadEnter` is distinct from
`Key::Enter`; `Key::Shift`, `Key::Control`, and `Key::Alt` mean either side.
Key names follow [Windows virtual-key mappings](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes).
Punctuation depends on the keyboard layout; these are key states rather than text.
System shortcuts still follow Windows behavior.

```cpp
if (input.held(Key::W)) { /* move forward */ }
if (input.pressed(Key::R)) { /* reset */ }
if (input.released(Key::Space)) { /* space was released */ }
if (input.pressed(MouseButton::Left)) { /* fire */ }
if (input.held(MouseButton::Right)) { /* aim */ }
```

Mouse buttons are `Left`, `Right`, `Middle`, `Back`, and `Forward`.
`mouseX`/`mouseY` are client-area pixels, with the origin at the top-left;
they use the window's size, including any letterboxing. `mouseDeltaX` and
`mouseDeltaY` accumulate movement for the current frame: positive means right
and down. The Windows backend uses [raw mouse input](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawmouse)
so captured movement continues at screen edges. Multiply movement by sensitivity,
without multiplying by elapsed seconds:

```cpp
yaw += input.mouseDeltaX * .0025f;
pitch = std::clamp(pitch + input.mouseDeltaY * .0025f, -1.2f, 1.2f);
player.transform.localRotation = Quaternion::fromEuler({0, yaw, 0});
camera.transform.localRotation = Quaternion::fromEuler({pitch, 0, 0});
```

`mouseWheel` is vertical scrolling (positive is up/away); `mouseWheelHorizontal`
is horizontal scrolling (positive is right). One wheel step is 1; fractional steps
are retained. Movement and scrolling reset every frame. A press and release
between frames still produce both edges, so short taps/clicks are not discarded.
`input.focused` reports window focus; losing focus releases held inputs and
discards pending movement and scrolling.

Return `true` from `Game::captureMouse()` for mouse look. The engine hides and
locks the cursor at the client center only while your window is focused, and
restores it on focus loss or exit. Captured mouse clicks cannot activate resize
borders or title-bar buttons. `mouseX`/`mouseY` stay near the center during capture;
use raw deltas for looking. Its default is `false` for games that need a free cursor. To quit
on Escape, handle `input.pressed(Key::Escape)` in your game and return `true` from
`shouldQuit()`; the engine also handles the window's close button and Alt+F4.

## Simple physics

Include `tiny3d/physics.hpp`. Components and simulation are in `physics.hpp` and
`src/physics.cpp`, independently of rendering and input.

- `BoxCollider`: local `center` and full local `size` (default 1 in each axis).
- `SphereCollider`: local `center` and `radius` (default .5).
- `CapsuleCollider`: local `center`, `radius` (default .5), and full `height`
  including caps (default 2). Its axis is local Y; height must be at least twice
  the radius. It rotates with its entity and inherits the parent's transform.
- `Rigidbody`: `velocity`, positive `mass`, `useGravity`, and `isKinematic`.

Use one enabled collider and one rigidbody per entity. A collider without an
enabled rigidbody is static. Dynamic bodies integrate velocity and gravity;
collisions separate overlapping bodies and stop their relative inward velocity.
Mass controls how much each dynamic body moves during a collision. Kinematic
bodies are unaffected by gravity and impulses; scripts control their movement.
Rendering visibility does not disable physics. Disable the collider component
to turn off its collisions.

Own a `Physics` instance in your game and call `physics.step(scene, seconds)`
from `Game::update`, after scripted movement. It uses small substeps internally.
Its configurable `gravity` defaults to `{0, -9.81f, 0}`. A body without a collider
still moves, but has no collision response.

```cpp
Entity& player = scene.entities.emplace_back();
auto& capsule = player.addComponent<CapsuleCollider>();
capsule.center = {0, .9f, 0};
capsule.radius = .35f;
capsule.height = 1.8f;
player.addComponent<Rigidbody>().isKinematic = true;
player.transform.localPosition = {0, 0, -5};
Entity& camera = scene.entities.emplace_back();
camera.addComponent<Camera>();
camera.setParent(&player, false);
camera.transform.localPosition = {0, 1.5f, 0};

// In update: move the capsule player; the camera follows through parenting.
physics.move(scene, player, movement * seconds);
physics.step(scene, seconds);
```

`Physics::move` breaks displacement into small pieces and corrects overlap so
the moved entity stops and slides against colliders. It does not push other
entities; direct transform writes are teleports and bypass this movement path.
Collider centers and dimensions follow the complete entity hierarchy. Sphere
radius uses the largest world axis scale. Capsule radius uses the larger X/Z
scale, and height uses Y scale, clamped to at least the world diameter. Sheared
hierarchies are approximated with round sphere/capsule shapes. Boxes use world
axis-aligned bounds, including the enclosure of rotated boxes. Rigidbody velocity
and `Physics::move` displacement are world vectors. Ancestors and descendants do
not collide with each other; compound rigidbodies are not implemented.
This is deliberately linear physics: no angular
motion, friction, bouncing, triggers, or continuous collision detection. Keep
time steps and movement small; substeps are capped at 128 per call.

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
