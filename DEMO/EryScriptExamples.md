# EryScript examples

The collectible animation in game.cpp runs real EryScript components. The
engine API is discovered through C++26 reflection; the demo also exposes
std::sin directly through reflection, without a conversion lambda.

## Attach a script

~~~cpp
#include "tiny3d/eryscript.hpp"

auto& cube = scene.entities.emplace_back();
cube.addComponent<tiny3d::MeshRenderer>(
    std::make_shared<tiny3d::Mesh>(tiny3d::cube()));
auto& script = cube.addComponent<tiny3d::EryScript>();
script.code = R"ery(
using tiny3d
var angle = 0
method Awake
    entity.getComponent<MeshRenderer>().color = new Color(255, 195, 60)
mend
method Update
    angle = angle + deltaTime
    entity.transform.localRotation = Quaternion.fromEuler(new Vector3(0, angle, 0))
mend
)ery";
~~~

The engine calls parameterless Awake once and Update each frame. The
runOnAwake/runInUpdate flags default to true. Missing methods are skipped.
Call other script methods through script.runtime().call("MethodName").

## Keyboard, mouse, and transforms

~~~text
using tiny3d
var yaw = 0
method Update
    if input.held(Key.W)
        entity.transform.localPosition.z = entity.transform.localPosition.z + 4 * deltaTime
    end
    yaw = yaw + input.mouseDeltaX * 0.0025
    entity.transform.setRotation(Quaternion.fromEuler(new Vector3(0, yaw, 0)))
    if input.pressed(MouseButton.Left)
        Console.WriteLine("Click! Wheel: " + input.mouseWheel)
    end
mend
~~~

Use the actual C++ names and signatures. input.held/pressed/released overloads
accept either Key or MouseButton constants; reflected constants select the
correct overload. Input fields are read-only. mouseX/Y, mouseDeltaX/Y,
mouseWheel, mouseWheelHorizontal and focused are discovered automatically.

The script root exposes entity, input, deltaTime and time. Nested fields are
live, so entity.transform.localPosition.y = 2 updates the entity directly.
World transforms use position()/rotation()/lossyScale() and setPosition(...)/
setRotation(...). Quaternion.fromEuler takes one Vector3 in radians;
fromEulerDegrees takes degrees. toEuler()/toEulerDegrees() return Vector3.
Color channels range from 0 to 255. C++ collections convert to array snapshots.

The old aliases GetComponent, Time, Input, FromEuler and Tiny3D were removed.
Use entity.getComponent<Camera>(), deltaTime, input and using tiny3d instead.
Entity.parent(), childCount() and getChild(index) are ordinary reflected methods.
Inherited Component.enabled and all public component fields are discovered too.

## Button callback

~~~text
using tiny3d
var clicks = 0
method Awake
    entity.getComponent<Button>().onClick = Clicked
mend
method Clicked
    clicks = clicks + 1
    Console.WriteLine("Clicks: " + clicks)
mend
~~~

Attach this to an entity with a Button and Image inside a Canvas. Callbacks
become harmless when their script stops, reloads, restarts, or is destroyed.
Callback errors are recorded in component.error() and stop that script.

## Expose a project namespace

Declare normal C++ APIs; no field lists or conversion lambdas are needed:

~~~cpp
#include "eryscript/reflection.hpp"

namespace Gameplay {
struct Stats {
    int health = 100;
    void heal(int amount) { health += amount; }
};
inline double total(const std::vector<double>& values) {
    double sum = 0;
    for (double value : values) sum += value;
    return sum;
}
}

// Before the first engine frame:
script.reflection().bindNamespace<^^Gameplay>(script.runtime());
~~~

~~~text
using Gameplay
var stats = new Stats()
method Awake
    stats.heal(10)
    Console.WriteLine(stats.health)
    var values = new double[2]
    values[0] = 2
    values[1] = 3
    Console.WriteLine(total(values))
mend
~~~

The output is 110 and 5. Adding another public Stats field or method makes it
available after recompiling that call site. Include the API headers there:
reflection sees declarations visible to that translation unit. Component
getComponent<T>/addComponent<T> specializations are generated for component
types visible in the namespace used to discover Entity.

## Call the standard library directly

~~~cpp
#include "eryscript/reflection.hpp"
#include <cmath>

constexpr auto sine = std::meta::reflect_function(
    *static_cast<double(*)(double)>(std::sin));
script.reflection().bindFunctions<sine>(script.runtime());
~~~

The script can now call sin(angle). The C++ cast chooses the double overload;
reflection discovers the function's name, parameters and return type. There
is no hand-written native invocation or argument-conversion code. Select other
compiled overloads or template specializations the same way. Reflection is
compile-time API discovery, so scripts cannot instantiate arbitrary new C++
templates while running.

## Standalone use

~~~cpp
#include "eryscript/reflection.hpp"

Gameplay::Stats stats;
eryscript::Runtime runtime;
eryscript::Reflection reflection;
reflection.bindNamespace<^^Gameplay>(runtime);
runtime.load("method Heal\n heal(5)\n return health\nmend", reflection.object(&stats));
double health = runtime.call("Heal").number(); // 105
~~~

Keep the Reflection instance and host objects alive while their views are used.
This code depends only on Engine/EryScript, not the engine or Unity.

## Run the checks

ReflectionChecks.cpp verifies a new host API without any engine binding edits,
including constructors/default arguments, private-member protection, inheritance,
live nested fields, enum overloads, standard-library calls, ref/out, callbacks,
reload/restart, input and scene lifetime checks.

After building the engine in Debug, run from the repository's MSYS2 UCRT64 terminal:

~~~bash
g++ -std=c++26 -freflection -Wall -Wextra -Wpedantic -I Engine/include -I Engine/EryScript/include DEMO/ReflectionChecks.cpp Engine/build/GCC/Debug/libtiny3d.a Engine/build/GCC/Debug/EryScript/liberyscript.a -static -luser32 -lgdi32 -o Engine/build/reflection-checks.exe
Engine/build/reflection-checks.exe
~~~

It prints "Reflection checks passed"; one logged script error is deliberately
triggered to verify callback error handling. The check source is optional and
is not included in the playable project's source manifest.
