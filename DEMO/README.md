# Collector demo

Open `Engine/Tiny3D.sln` in Visual Studio 2022 and set `Engine/engine.json` to:

```json
{
  "project": "../DEMO",
  "mode": "editor",
  "autoStart": true
}
```

Choose **Debug | x64** or **Release | x64**, then press **Ctrl+Shift+B** to build
and start the demo. Both `editor` and `game` modes work the same for now.
Set `autoStart` to `false` when using **F5** to debug.

Collect the five gold cubes by walking near them. Use **WASD** to move,
**the mouse** to look, **R** to reset, and **Escape** to exit. Progress appears
in the window title. Blue blocks and the floor have box colliders. A player entity
has a capsule collider and a kinematic rigidbody: it slides along blocks, and
gravity does not move it. The camera is its child, with an eye-height local offset
and no collider. Mouse yaw rotates the player; pitch rotates the camera locally.
Gold cubes remain collectibles you can walk through.
The cursor is hidden and locked at the center while the demo is focused. Captured
clicks cannot resize or close the window through its border. Switching to another
window releases it. Mouse sensitivity is `mouseSensitivity` in `game.cpp`.

The entire `DEMO` folder is optional. After deleting it, select your own project
or set `project` to `""` to build only the engine library. Build and API
instructions are in [Engine/README.md](../Engine/README.md).
