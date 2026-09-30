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
**arrow keys** to look, **R** to reset, and **Escape** to exit. Progress appears
in the window title. Blue blocks are scenery; there is no collision or physics.

The entire `DEMO` folder is optional. After deleting it, select your own project
or set `project` to `""` to build only the engine library. Build and API
instructions are in [Engine/README.md](../Engine/README.md).
