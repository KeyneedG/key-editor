# Demo projects

Everything in this folder is optional. Delete the entire `DEMO` folder to remove
the samples and their checks. The engine and editor build tools remain usable.

## Collector

The main sample is `game.cpp` with its `project.json`. Collect the five gold cubes
by walking near them. Use **WASD** to move, **arrow keys** to look, **R** to reset,
and **Escape** to exit. Progress appears in the window title. Blue blocks are
scenery; there is no solid-object collision or physics.

To use Visual Studio 2022, open `Editor/Tiny3D.sln` and set `Editor/engine.json` to:

```json
{
  "project": "../DEMO",
  "mode": "editor",
  "autoStart": true
}
```

Build the solution to compile and automatically start it. Switch `mode` to `game`
and rebuild to exercise game mode. Both modes intentionally work the same today.
Set `autoStart` to `false` when using F5 to debug.

The sample-specific settings can also be passed to the build script from the
engine root:

```powershell
.\Editor\build.ps1 -Configuration Release -ConfigFile .\DEMO\editor.json
.\Editor\build.ps1 -Configuration Release -ConfigFile .\DEMO\game.json
```

Run the demo checks with:

```powershell
.\DEMO\check.ps1
```

These build both modes without automatic startup, render an image in each mode,
compare them, and exercise the native window with a three-frame run. Generated
images stay in `DEMO/build`.

## Minimal game

`minimal/` contains a single rotating cube and a source manifest. Use that game's
factory and `Game` subclass as a starting point for a new project. To run it, set
`project` to `../DEMO/minimal` in `Editor/engine.json` and build.

After deleting these samples, select your own project, or set `project` to `""`
in `Editor/engine.json` to build only the engine library.
