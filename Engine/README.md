# Tiny3D

A small, extensible C++26 3D engine with no third-party libraries. The optional
Direct3D 11 module renders native Windows games and the editor on the GPU. The
software renderer remains available for fallback and headless rendering. Window
and input handling use Win32. JSON needs no separate library.

```text
Engine/
  include/tiny3d/    Public math, engine, physics, UI, and project API
  src/              Renderer, physics, UI, window/input loop, and project entry point
  EryScript/        Standalone interpreter and generic C++26 reflection bridge
  Rendering/        Software presentation and optional Direct3D 11 module/shaders
  Tiny3D.sln        Visual Studio editor solution
  Tiny3D.vcxproj    Visual Studio project wrapping the GCC C++26 build
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
  EryScriptExamples.md  Script and reflection examples
  ReflectionChecks.cpp  Optional reflection verification/example
```

All engine files belong to `Engine`; all sample files belong to `DEMO`.
You can delete `DEMO`. Select your own project, use an empty `project` in editor
mode, or use an empty `project` in game mode to build only the libraries. Generated output stays in `Engine/build`
and can be deleted.

## Visual Studio 2022

1. Keep **Desktop development with C++** and the v143 toolset installed for the
   Visual Studio project system. Install MSYS2 in `C:\msys64`, update it, and run
   this command in its **UCRT64** terminal:

   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gdb
   ```

   GCC 16 or newer is required. The build checks `<meta>` and actual reflection
   expressions during configuration. GCC compiles every target with
   `-std=c++26 -freflection`. The Windows executable statically links the GCC
   runtime, so launching it does not depend on adding GCC to your Windows PATH.
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
   `editor` opens the scene editor; `game` runs the project's game. Set `project`
   to an empty string in editor mode to start with no project selected.
4. Choose **Debug | x64** or **Release | x64** and press **Ctrl+Shift+B**.
   A successful build starts the selected project automatically. Close it before
   rebuilding. The executable is
   `Engine/build/GCC/<configuration>/tiny3d_project.exe`.
5. GCC produces DWARF debug information. The build writes `gdb.xml` beside the
   executable to configure Visual Studio's built-in MIEngine for local GDB.
   After a Debug build, open **View > Other Windows > Command Window** and enter
   this command, substituting your repository's absolute path:

   ```text
   Debug.MIDebugLaunch /Executable:Tiny3D /OptionsFile:"D:\Projects\Git\key-editor\Engine\build\GCC\Debug\gdb.xml"
   ```

   Set `autoStart` to `false` when debugging to avoid launching a separate game
   during the build. This command uses the GDB engine; ordinary **F5** still uses
   the solution's Windows native debugger, which cannot read GCC symbols.
   You can also debug directly from PowerShell:

   ```powershell
   & C:\msys64\ucrt64\bin\gdb.exe Engine/build/GCC/Debug/tiny3d_project.exe
   ```

   Enter `break main`, then `run`. Visual Studio's **Ctrl+F5** can run the built
   executable without debugging.

Each build reads the settings and source manifest again. Rebuild after changing
the selected project or mode. The executable uses your project's directory as
its working directory, so relative asset paths belong to your game.

Engine sources and the demo's `game.cpp` appear together in Solution Explorer
while `DEMO` exists. For your own source files, use **Add > Existing Item** on the
editor project and save with **File > Save All**. This provides an editing context;
`project.json` controls which files compile. The editor uses its latest language
mode for IntelliSense, while GCC performs the actual C++26 compilation. VS 2022's
IntelliSense parser does not understand C++26 reflection yet; reflection code can
compile successfully despite IntelliSense diagnostics. Use **Build Only** in the
Error List to view compiler errors. Reload the solution after changing its
project file outside Visual Studio.

The default compiler tools are in `C:\msys64\ucrt64\bin`. For another MSYS2
location, set the `Tiny3DToolchainDirectory` MSBuild property or pass
`-ToolchainDirectory <installation>/ucrt64` to `build.ps1`. The build changes only
its own process's PATH. Existing consoles and their environment stay untouched.
Old MSVC output under `Engine/build/Debug` and `Release` is kept separate.

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

The script locates GCC, CMake, and Ninja in the selected UCRT64 directory.
If script execution is disabled:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Engine\build.ps1 -Configuration Release
```

## Scene editor

The editor is a separate `tiny3d_editor` library under `Engine/Editor`. Game builds
link the runtime library only. The editor uses the engine's Canvas, Image,
SimpleText, Button, ScrollRect, and InputField controls, and pauses scene scripts,
physics, and game updates while editing.

- **File:** New Scene, New Project, Open Project, Build Settings.
- **Edit:** Undo, Redo, Save. Shortcuts: Ctrl+Z, Ctrl+Y/Ctrl+Shift+Z, Ctrl+S.
- **Help:** About EryEngine.
- **Hierarchy:** select objects or use **+** to create an empty object, cube,
  plane, or camera. Children appear beneath their parents.
- **Inspector:** edit names, tags, layers, visibility, local transforms, parent
  index (-1 means no parent), and built-in component properties. Rotation fields
  use degrees; vectors are X/Y/Z and colors are R/G/B. Add or remove components.
  Camera/content reference fields use scene entity indices (-1 means no reference).
- **Project:** double-click folders to browse and scene JSON files to load them;
  Refresh reloads the directory listing. Wheel-scroll each panel independently.
- **Scene view:** click a mesh to select it. Use W/E/R or the toolbar to move,
  rotate, or scale; drag the colored primitive handles. Each drag is one undo
  step; Escape cancels it. F frames the selection. Hold the right mouse button
  to look and fly with WASD/QE; Shift speeds up movement. The wheel moves forward
  and backward.

Input fields commit on Enter or focus loss and restore their previous value on
Escape. Clicking a field selects its text; Ctrl+A selects all. Arrow keys,
Home/End, Backspace, and Delete edit text. EryScript code fields accept multiple
lines; Ctrl+Enter commits them.

**New Project** asks for a name and creates `Engine/Projects/<name>/Assets`, copies
the current project's content into Assets, and saves the current scene as
`Main.scene.json` (or the next available name when that scene already exists). It
creates a `project.json` with an empty `sources` array, so
the project can run directly from its saved startup scene. Build folders and
symlinks are excluded from the copy. **New Scene** immediately creates a scene
JSON in Assets. **Open Project** opens a native folder picker starting in
`Engine/Projects`; select the project folder containing Assets and project.json.
The chosen project is remembered in engine.json. Scene/project switches offer
Save, Discard, and Cancel when the current scene has unsaved edits.

**Build Settings** stores the startup scene, resolution, and fullscreen flag in
project.json. To run a scene-based project, set engine.json's mode to `game` and
rebuild. Existing C++ projects still use their own Game implementation and screen
settings; the editor initially imports their scene from createGame() when no
startup scene has been saved.

JSON persistence covers all built-in components, shared mesh geometry, hierarchy
links, tags, layers, visibility, and local transforms. UI-generated meshes are
rebuilt after loading. Native callbacks, script execution state, and project-specific
reflection bindings are runtime code and must be reattached by C++ projects.
Custom C++ components require a serializer; saving reports unsupported components
instead of silently dropping them. Undo/redo uses up to 64 scene snapshots.

Include `tiny3d/scene.hpp` to use `serializeScene`, `deserializeScene`, `saveScene`,
and `loadScene` from game code. Scene loading constructs a fresh scene and rejects
bad links, cycles, mesh indices, and camera projections. Save replaces the file
only after the complete JSON has been written.

## Rendering backends

Managed native executables default to `"renderer": "auto"`: try Direct3D 11
hardware rendering and fall back to software if device initialization fails.
Set `renderer` in engine.json to `"d3d11"` to require the GPU backend or
`"software"` to use the original renderer. The setting is read at startup; no
rebuild is required to switch an already included backend. The console reports
the backend in use. You can also override it on the command line:

```powershell
Engine/build/GCC/Release/tiny3d_project.exe --renderer d3d11
Engine/build/GCC/Release/tiny3d_project.exe --frames 60 --renderer software
```

Direct3D types, resources, HLSL shaders, and swap-chain handling live entirely
inside `Rendering/Direct3D11`, built as `tiny3d_d3d11`. It supports the existing
perspective/orthographic cameras, layer masks, depth ordering, color clearing,
parent transforms, flat lighting, unlit UI, and letterboxing. Meshes are uploaded
once and cached while alive. When editing a shared mesh's arrays in place, call
`mesh.markChanged()` afterwards; replacing the mesh also updates its GPU buffers.
Unchanged UI text and scroll clipping retain their meshes between frames.

The engine's `tiny3d/graphics.hpp` defines the small `RenderBackend` interface:
resize, render, present, dimensions, and explicit pixel readback. The window loop
accepts a `graphics::BackendFactory`; it owns no Direct3D objects. Other APIs can
implement this interface in another module and supply their own factory. Existing
calls to `run(game)` still work using the software backend. For a custom loop or
executable using Direct3D, link `tiny3d_d3d11` and provide the factory:

```cpp
#include "tiny3d/d3d11.hpp"
tiny3d::run(game, 800, 500, 0, [](void* window, int width, int height) {
    return tiny3d::d3d11::createRenderer(window, width, height);
});
```

The Windows module builds by default. Configure with `-DTINY3D_ENABLE_D3D11=OFF`
to omit it; non-Windows and `TINY3D_HEADLESS` builds omit it automatically. It uses
system D3D11/DXGI/D3DCompiler libraries and feature level 11.0, with a Windows 10+
flip-model swap chain. Shader sources are embedded at build time, so launching
does not depend on their directory. A device/presentation failure after startup
is reported; automatic device-loss recovery is outside this basic module.

`--render file.ppm` uses software by default and works without a native window.
Add `--renderer d3d11` to render offscreen with hardware and explicitly read the
frame back. Normal GPU rendering/presentation does not copy pixels to the CPU.
The optional `d3d11_checks` test uses WARP for repeatable GPU-pipeline validation;
run `tiny3d_d3d11_checks.exe --hardware` to validate on the actual GPU. It compares
rendered images against the software renderer and exercises resize/presentation.

API references: [Direct3D device creation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)
and [swap-chain resizing](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers).

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
- Optionally `screenSettings() const` to request a rendering size and window mode.

Implement `std::unique_ptr<tiny3d::Game> tiny3d::createGame()` to construct your
game. The engine supplies `main()` through `src/project_main.cpp`.

Own the scene in your game class. Each `Entity` has a name, transform, visibility flag,
`layer` (0 by default), `tag` (`"Untagged"` by default), and an owned list of components.
`Component` is abstract with a virtual destructor;
derive from it to add your own component types. Use `addComponent<T>(...)` to
construct a component and `getComponent<T>()` to find the first matching component
(or `nullptr`). Components have an `enabled` flag.

`MeshRenderer` combines a shared mesh and its color in one component. `Camera`
is a component too; its position and rotation come from its entity's transform.
The renderer draws enabled cameras on visible entities in increasing `depth` order.
Negative depths work; equal depths follow scene and component order. Each camera
clears its depth buffer. `clearColor` defaults to `true`; set it to `false` for an
overlay camera that retains earlier cameras' color. Without an eligible camera,
the frame contains only the background. Camera scale does not affect the view.

Layers are numbered 0-31. `Camera::layers` is a 32-bit mask and defaults to all
layers. Use `camera.layers = layerMask(0)` or combine masks with `|`. Layers filter
rendering and UI picking. Tags are plain strings you can use in game logic.
`Camera::orthographic` defaults to `false`; when enabled, `orthographicSize` is
half the vertical view size in world units. `MeshRenderer::unlit` draws exact
component colors, which is useful for UI.

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
The Windows backend composes the frame and letterbox bars in a reusable offscreen
bitmap, then copies the complete result to the window in one operation. This avoids
flashing the black background between frames.
Update your custom component behavior from `Game::update`. Input exposes
`held`, `pressed`, and `released` for both keyboard keys and mouse buttons.

## EryScript components

Include tiny3d/eryscript.hpp and add an EryScript component. Its public code
string contains the source; runOnAwake and runInUpdate both default to true.
The engine initializes scripts before the first paint, calls parameterless
Awake once, then calls parameterless Update after Game::update each frame.
Missing methods are skipped. Newly added or reenabled components initialize on
their first scripting tick; source edits recompile and reset their variables.
Top-level statements execute once per initialization, even when both hook flags
are false. Disabling Component::enabled suspends automatic execution without
resetting variables; hiding an entity only affects rendering.

The selected scene must be mutable. Existing scene() const overrides still work
for games that own a mutable Scene; new games can also override Scene& scene()
to return that scene directly. --render follows the same scripting lifecycle,
with zero delta time. A custom loop can own Scripts and call awake(scene) once,
then update(scene, seconds, input) each frame.

C++26 reflection discovers the public types, fields, methods, constructors,
enums and functions visible in the `tiny3d` namespace. There are no per-member
engine binding tables. Scripts use the C++ names and signatures:
`entity.getComponent<Camera>()`, `entity.getChild(0)`, `entity.parent()`,
`entity.transform.position()`, `entity.transform.setPosition(value)`,
`Quaternion.fromEuler(new Vector3(0, angle, 0))`, `input.held(Key.W)` and
`layerMask(1)`. Start scripts with `using tiny3d` to use namespace members.

The script root (`this`) provides `entity`, read-only `input`, `deltaTime` and
`time`. Nested object fields are live: `entity.transform.localPosition.y = 2`
updates the entity directly. World transforms use the C++ getter/setter methods.
`Quaternion.fromEuler` uses radians; `fromEulerDegrees` uses degrees. C++ const
objects are read-only. Collections convert to script arrays; these containers
are snapshots, so replace a writable collection field to write it back.

The former hand-written aliases (`Tiny3D`, `GetComponent`, `Time`, `Input`,
`FromEuler`, etc.) have been removed. See the updated examples for migration.
To expose project code, include `eryscript/reflection.hpp` and call
`component.reflection().bindNamespace<^^YourNamespace>(component.runtime())`
before its first frame. Existing compiled functions, including standard-library
overloads, can be selected with `bindFunctions`; names and argument conversions
are generated automatically. Reflection discovers declarations visible in the
translation unit making that call, so include your API headers there.

Keep scene entity slots stable while scripts refer to other entities, as with
other raw entity pointers. Clearing and rebuilding a scene stops old script
callbacks. Script errors are logged once, stored in component.error(), and stop
that script while the game continues. Edit code or call restart() to retry.
component.runtime() provides explicit script method calls; component.reflection()
provides additional API discovery. Native templates require compiled
specializations; this bridge generates single-type member-template calls for
types visible in the selected namespace (including component lookup/creation).
It does not compile arbitrary new C++ templates while a script is running.
Changed UI graphics are refreshed after scripting, without processing clicks twice.

The standalone interpreter is in [EryScript](EryScript/README.md), without engine
or Unity dependencies. [Examples](../DEMO/EryScriptExamples.md) and the demo's
scripted collectible animation are entirely in DEMO.

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
does not invalidate entity references or parent pointers. `getChild(index)` returns
a direct child in parenting order, or `nullptr` for an invalid index; const access
is available too. `childCount()` reports the number of direct children. Reparenting
updates both child lists. Destroying a parent detaches its children while retaining
world position, rotation, and scale magnitudes. Keep entities in their scene slots when other components hold
raw pointers to them.

`visibleInHierarchy()` includes ancestor visibility. Hiding a parent hides its
rendered descendants and stops UI picking; it does not disable their physics.

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

## Canvas UI

Include `tiny3d/ui.hpp` and own a `UI` instance in your game. Call
`ui.update(scene, input)` each frame, including while gameplay is paused. The UI
module lives in `src/ui.cpp` and uses the normal mesh renderer. It requires no
textures, font files, operating-system font rendering, or external libraries.

- `Canvas` identifies a world-space UI hierarchy. Assign its `camera` and place
  the entity in front of that camera, usually as the camera's child.
- `Image` is an XY rectangle with `width`, `height`, and a solid `color`.
- `SimpleText` builds a 5x7 font from small XY planes. Set `text`, `color`, and
  `pixelSize` in local units. Letters, digits, common punctuation, spaces, and
  newlines work; lowercase uses uppercase shapes, and unsupported bytes use `?`.
  `centered` defaults to `true`; `false` places the top-left at the entity origin.
- `Button` belongs on an entity with an `Image`. Set `onClick` and optionally
  its normal/hover/pressed/disabled colors. `interactable` disables clicks;
  `hovered` and `pressed` report the current UI state. Put a text label on a child.

Use one graphic (`Image` or `SimpleText`) per entity. UI creates or updates that
entity's `MeshRenderer`; geometry is rebuilt only when dimensions or text change.
All graphics are unlit and face local -Z. Assign each graphic to the UI layer;
entity layers are not inherited. Place label planes slightly nearer the camera
than the button plane to avoid equal-depth overlap.

```cpp
Entity& uiCamera = scene.entities.emplace_back();
auto& view = uiCamera.addComponent<Camera>();
view.layers = layerMask(1);
view.depth = 10;
view.clearColor = false;
view.orthographic = true;
view.orthographicSize = 250; // 500 world units vertically, independent of pixel resolution.

Entity& canvas = scene.entities.emplace_back();
canvas.setParent(&uiCamera, false);
canvas.transform.localPosition = {0, 0, 5};
canvas.addComponent<Canvas>().camera = &uiCamera;

Entity& button = scene.entities.emplace_back();
button.layer = 1;
button.setParent(&canvas, false);
button.addComponent<Image>().width = 200;
button.addComponent<Button>().onClick = [] { /* Handle the action. */ };

Entity& label = scene.entities.emplace_back();
label.layer = 1;
label.setParent(&button, false);
label.transform.localPosition.z = -.02f;
label.addComponent<SimpleText>().text = "RESUME";
```

Set the world camera's mask to `layerMask(0)` for this two-camera setup. Rectangular
button picking supports perspective and orthographic cameras, hierarchy transforms,
and letterboxing through `Input::viewport`, `renderWidth`, and `renderHeight`.
A click requires pressing and releasing over the same button. Focus loss cancels
the press. Hidden, disabled, masked, or back-facing buttons do not receive clicks.
Picking selects the nearest button for the camera with the highest depth; it does
not test occlusion against non-button meshes. Keep the assigned canvas camera alive.
Call `ui.cancel()` when rebuilding a scene or switching menus.

An **InputField** shares an entity with an Image. Set `text`, `placeholder`,
`pixelSize`, `maxLength` (UTF-8 bytes), and `onSubmit(const std::string&)`; the UI
creates its text graphics. Set `multiline` to accept line breaks. The Windows
backend collects actual typed characters in `Input::text`, independently of key
states. SimpleText's existing font still supplies the glyphs.

A **ScrollRect** shares an entity with an Image defining the clip rectangle.
Assign a direct child to `content`, set `contentHeight`, and optionally set
`offset` and `wheelSpeed`. Its child's initial local position is the origin;
positive offsets move content upwards. Descendant Image/SimpleText/InputField
meshes and button/input picking are clipped to the rectangle, including nested
scroll rectangles. `contentPosition()` reports the unscrolled position for saving.

## Screen settings

Override `Game::screenSettings()` and return your current requested settings:

```cpp
ScreenSettings screen_{800, 500, false};
ScreenSettings screenSettings() const override { return screen_; }
// In a settings button callback:
// screen_ = {1280, 720, true};
```

The Windows backend applies changes after `update`. Width and height set both the
rendering resolution and the windowed client size; a zero dimension keeps the
current size. Fullscreen is a borderless window covering the current monitor,
with the selected rendering resolution fitted into it. Returning to windowed mode
restores the previous position and the selected client size. Manual resizing
letterboxes the rendered image until you request a different resolution.
The backend updates `Input::viewport` for correct UI clicks. Settings remain in
memory for the current run; persistence is up to your game.

## Direct CMake build

From **MSYS2 UCRT64**, in the repository directory:

```bash
cmake -S Engine -B Engine/build/GCC/Release -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build Engine/build/GCC/Release
```

Without `TINY3D_CONFIG`, this builds the engine library only. To select a game,
add `-DTINY3D_CONFIG=<absolute-settings-path>` to the configuration command.
Automatic startup can be disabled with `-DTINY3D_AUTO_START=OFF`.

Other CMake programs can use `add_subdirectory` with the `Engine` directory and
link against `tiny3d`. An independent executable can provide its own `main()` and
call `tiny3d::run(game)`.

The core can also build on Linux and macOS with CMake 3.25+ and a compiler that
supports C++26 reflection, such as GCC 16+.
Use `-DTINY3D_HEADLESS=ON` to omit the native window backend. The Visual Studio
wrapper and automatic startup are Windows features.

Managed executables accept `--render <file.ppm>` for a headless RGB frame and
`--frames <count>` for a bounded native-window run.

Optional integration checks: configure with `-DTINY3D_BUILD_TESTS=ON`, build, then
run `ctest --test-dir <build-directory> --output-on-failure`. These exercise scene
round-trips, invalid input, UI clipping/text input, project and scene workflows,
editor menus, gizmo undo/redo, game rendering, and an editor with no project.
