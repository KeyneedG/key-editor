# EryScript examples

The collectible animation in game.cpp is an actual EryScript component: Awake
sets its initial rotation; Update advances a per-cube age and changes its height
and rotation. The demo disables these components while paused or collected.

## Attach a script

~~~cpp
#include "tiny3d/eryscript.hpp"

tiny3d::Entity& cube = scene.entities.emplace_back();
cube.addComponent<tiny3d::MeshRenderer>(
    std::make_shared<tiny3d::Mesh>(tiny3d::cube()));
auto& script = cube.addComponent<tiny3d::EryScript>();
script.runOnAwake = true;
script.runInUpdate = true;
script.code = R"ery(
var angle = 0

method Awake
    GetComponent<MeshRenderer>().color = new Color(255, 195, 60)
mend

method Update
    angle = angle + Time.deltaTime
    transform.localRotation = Quaternion.FromEuler(0, angle, 0)
mend
)ery";
~~~

The native engine loop invokes these methods automatically. They take no
parameters. Missing Awake or Update methods are simply skipped. All ordinary
EryScript methods can be called from C++ after initialization:

~~~cpp
if (script.runtime().hasMethod("Reset"))
    script.runtime().call("Reset");
~~~

## Keyboard, mouse, and transforms

~~~text
var yaw = 0

method Update
    var position = transform.localPosition
    if Input.GetKey(Key.W)
        position.z = position.z + 4 * Time.deltaTime
    end
    transform.localPosition = position

    yaw = yaw + Input.mouseDeltaX * 0.0025
    transform.localRotation = Quaternion.FromEuler(0, yaw, 0)

    if Input.GetMouseButtonDown(MouseButton.Left)
        Console.WriteLine("Click! Wheel: " + Input.mouseWheel)
    end
mend
~~~

Vector3, Quaternion and Color values are snapshots: edit a variable and assign
it back, as with position above. transform.localPosition.y = 2 edits a temporary
snapshot and does not update the entity. Use a whole-value assignment.
Quaternion.FromEuler uses radians. FromEulerDegrees and the Unity-compatible
Euler alias use degrees. ToEuler and ToEulerDegrees return Vector3 objects.
Color uses red/green/blue channels from 0 to 255, matching the engine.

Input exposes all engine Key names, including LeftArrow and NumpadEnter. GetKey,
GetKeyDown/GetKeyUp and GetMouseButton/Down/Up accept enum values or names such
as "W" and "Left". mouseX/Y, mouseDeltaX/Y, mouseWheel, mouseWheelHorizontal and
focused are properties. Mouse movement already describes this frame; multiply
by sensitivity, without deltaTime.

## Arrays, loops, and methods

~~~text
var samples = new float[4]

method Fill
    for i = 0 to samples.Length - 1
        samples[i] = i * 2
    end
mend

method Total
    var result = 0
    for i = 0 to samples.Length - 1
        if samples[i] == 0
            continue
        end
        result = result + samples[i]
    end
    return result
mend
~~~

After initialization, call Fill and then Total from C++; the latter returns 12:

~~~cpp
script.runtime().call("Fill");
double total = script.runtime().call("Total").number();
~~~

## Button callback

Attach this code to an entity that already has an Image and Button in a Canvas:

~~~text
var clicks = 0

method Awake
    GetComponent<Button>().onClick = Clicked
mend

method Clicked
    clicks = clicks + 1
    Console.WriteLine("Clicks: " + clicks)
mend
~~~

Callbacks stop working after the script stops, reloads source, or is destroyed.
Errors in script button callbacks are recorded by the owning component.

## Standalone C++ and your own bindings

This example uses only the EryScript library, with no Tiny3D includes:

~~~cpp
#include "eryscript/eryscript.hpp"
#include <iostream>

eryscript::Runtime script;
script.bind("Double", eryscript::Value(eryscript::Function(
    [](eryscript::Arguments& args) {
        args.requireCount(1);
        return eryscript::Value(args.values[0].number() * 2);
    })));
script.load(R"ery(
method Calculate(x)
    return Double(x) + 1
mend
)ery");
std::cout << script.call("Calculate", {10}).number(); // 21
~~~

The same bind API is available through component.runtime() for exposing your
own C++ behavior. Add bindings before the first engine frame if Awake needs them.
The standalone library is in Engine/EryScript; the engine-specific bindings are
in Engine/src/eryscript.cpp.
