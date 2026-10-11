# Collector demo

Open `Engine/Tiny3D.sln` in Visual Studio 2022 and set `Engine/engine.json` to:

```json
{
  "project": "../DEMO",
  "mode": "game",
  "autoStart": true
}
```

Choose **Debug | x64** or **Release | x64**, then press **Ctrl+Shift+B** to build
and start the demo. Use `editor` mode to inspect and edit its initial scene.
Press **Play F5** in the editor to test the current scene, **Pause** or **Step**
to inspect simulation, and **Stop F5** to return to the unchanged editing scene.
Escape releases the editor play cursor; click the game to capture it again.
The solution builds through GCC with C++26 reflection enabled. Use GDB for source
debugging; setup and commands are in [Engine/README.md](../Engine/README.md).

Collect the five gold cubes by walking near them. Use **WASD** to move,
**the mouse** to look, **R** to reset, and **Escape** to open the pause menu. Progress appears
in the window title. Blue blocks and the floor have box colliders. A player entity
has a capsule collider and a kinematic rigidbody: it slides along blocks, and
gravity does not move it. The camera is its child, with an eye-height local offset
and no collider. Mouse yaw rotates the player; pitch rotates the camera locally.
Gold cubes remain collectibles you can walk through.
Their floating and spinning animation runs through EryScript components saved
in `Assets/Untitled.scene.json`. Each cube has its own script variables. See
[EryScriptExamples.md](EryScriptExamples.md) for engine and standalone examples.
The cursor is hidden and locked at the center while the demo is focused. Captured
clicks cannot resize or close the window through its border. Switching to another
window releases it. Player speed and mouse sensitivity are Inspector fields on
the `C++: PlayerMovement` component attached to the saved Player object.

The pause menu has **Resume**, **Settings**, and **Quit**. It releases the mouse
and pauses movement, physics, and collectible animation. Escape resumes from the
main menu, or returns from Settings to the main menu.

In Settings, click **Size** to cycle through **800x500**, **1024x768**, **1280x720**,
**1600x900**, and **1920x1080**. Click **Mode** to toggle windowed or borderless
fullscreen, then **Apply**. **Back** returns to the pause menu. Fullscreen fills
the current monitor and letterboxes the selected render size when necessary.
Changes last for the current run.

The world camera renders layer 0; the orthographic UI camera renders layer 1 over
it. Menu entities are children of a world-space Canvas in front of the UI camera.
Images are solid-color planes, and the built-in text is made from small planes,
so the demo needs no image or font assets.

The entire `DEMO` folder is optional. After deleting it, select your own project
or set `project` to `""` in game mode to build only the engine library. An empty
project in editor mode opens the editor. Build and API
instructions are in [Engine/README.md](../Engine/README.md).

The collectibles use reflected engine APIs and a directly reflected `std::sin`
overload. See [EryScriptExamples.md](EryScriptExamples.md) for script names,
project namespace discovery and the optional `ReflectionChecks.cpp` checks.

## Native C++ components

`Assets/Scripts/Spin.cpp` demonstrates a registered `tiny3d::MonoBehaviour`. After
building, select a cube in the editor and choose **Add Component > C++: Spin**, or
double-click its `[C++]` Assets entry. Its `speed` field can be edited in the Inspector
and is saved with the scene. Open the Scripts folder in Assets to see its C++ files;
subfolder scripts are shown only when browsing that folder. Scripts run in game mode
and editor play; rebuild after changing C++.

`PlayerMovement.cpp` owns WASD movement, mouse look, collision correction, and player
reset. `CollectorController.cpp` manages collectible state and binds the saved menu
buttons at runtime. Both are registered native components attached in the scene JSON.
`game.cpp` only loads the startup scene and hosts UI/physics and window settings;
scene objects, cameras, geometry, and menus are authored in Assets.
