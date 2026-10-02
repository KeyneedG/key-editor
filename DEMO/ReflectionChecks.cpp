// Optional verification/example: all host members below are discovered automatically.
#include "eryscript/reflection.hpp"
#include "tiny3d/eryscript.hpp"
#include "tiny3d/physics.hpp"
#include "tiny3d/ui.hpp"
#include <iostream>

namespace Example {
inline int setting = 1;
enum class Mode { Idle, Active };
struct Point { float x = 0, y = 0; };
struct Base { int inherited = 12; virtual ~Base() = default; virtual int answer() const { return 1; } };
struct ReadOnly { const int value = 9; };
struct Counter : Base {
    inline static int limit = 50;
    int value = 4;
    Point position;
    std::vector<Point> points{{1, 2}};
    std::vector<bool> bits{false, true};
    Mode mode = Mode::Idle;
    std::function<void()> clicked;
    explicit Counter(int start = 4) : value(start) {}
    int add(int amount = 2) const { return value + amount; }
    int answer() const override { return 42; }
    static double twice(double value) { return value * 2; }
private:
    int secret = 99;
};
inline double choose(double value) { return value + .5; }
inline int choose(int value) { return value + 1; }
inline void increment(int& value) { ++value; }
inline void fill(int& value) { value = 17; }
inline double total(const std::vector<double>& values) {
    double sum = 0; for (double value : values) sum += value; return sum;
}
inline std::vector<Point> makePoints() { return {{4, 5}}; }
inline const Point& first(const std::vector<Point>& values) { return values.at(0); }
inline const Counter& identity(const Counter& counter) { return counter; }
inline void fillPoint(Point& value) { value = {6, 7}; }
enum class FirstChoice { Only = 1, Shared = 3 };
enum class SecondChoice { Only = 2, Shared = 3 };
inline int enumChoice(FirstChoice) { return 1; }
inline int enumChoice(SecondChoice) { return 2; }
inline double callTwice(const std::function<double(double)>& function, double value) {
    return function(value) + function(value + 1);
}
inline double optionalCallback(const std::function<double()>& function, double bonus = 1) {
    return function() + bonus;
}
struct Views {
    Point first{1, 2}, second{3, 4};
    Point* target = &first;
    std::vector<Point> points{{5, 6}};
};
} // namespace Example

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& function, const char* message) {
    try { function(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
void optimizedRuntime() {
    eryscript::Runtime runtime;
    eryscript::Reflection reflection;
    reflection.bindNamespace<^^Example>(runtime);
    runtime.load(R"ery(
using Example
var outer = 5
method Shadow(n)
    var total = 0
    for outer = 1 to n
        var local = outer
        for j = 1 to 2
            var local = j
            total = total + local
        end
        total = total + local
    end
    return total + outer
mend
method FreshIterations
    for i = 1 to 2
        if i == 1
            var previous = 9
        else
            return previous
        end
    end
mend
method DeclaredNull
    var value
    return value == null
mend
method FreshCalls(flag)
    if flag
        var once = 1
        return once
    end
    return once
mend
method Sum(n)
    if n <= 0
        return 0
    end
    return n + Sum(n - 1)
mend
method NestedCalls
    return Sum(4) + Sum(Sum(2))
mend
method Callback
    return callTwice(Sum, 3)
mend
method Reenter
    return optionalCallback(NestedCallback)
mend
method NestedCallback
    return optionalCallback(FinalCallback, 2)
mend
method FinalCallback
    return 5
mend
method Choice(value)
    return enumChoice(value)
mend
method Dynamic
    return injected
mend
method Controls
    var total = 0
    for i = 1 to 6
        if i == 2
            continue
        end
        if i == 5
            break
        end
        total = total + i
    end
    return total
mend
method BadRef
    return callTwice(ref outer, 1)
mend
)ery");
    check(runtime.call("Shadow", {3}).number() == 20 && runtime.get("outer").number() == 5,
        "Slot lookup changed nested scope shadowing");
    check(runtime.call("DeclaredNull").boolean(), "A declared null slot was treated as undefined");
    check(runtime.call("FreshCalls", {true}).number() == 1, "Conditional local declaration failed");
    rejects([&] { runtime.call("FreshCalls", {false}); }, "A call frame retained the previous call's locals");
    rejects([&] { runtime.call("FreshIterations"); }, "A loop iteration retained the previous iteration's locals");
    check(runtime.call("Sum", {8}).number() == 36 && runtime.call("NestedCalls").number() == 16,
        "Recursive/nested calls reused an active argument buffer");
    check(runtime.call("Callback").number() == 16, "Reentrant native callback arguments were overwritten");
    check(runtime.call("Reenter").number() == 8, "A reentrant call invalidated the cached overload signature");
    check(runtime.call("Controls").number() == 8, "Loop scope reuse changed break/continue");
    check(runtime.call("Choice", {1}).number() == 1 && runtime.call("Choice", {2}).number() == 2,
        "Overload cache ignored changed numeric values");
    rejects([&] { runtime.call("Choice", {3}); }, "Overload cache suppressed ambiguity");
    rejects([&] { runtime.call("Choice", {99}); }, "Overload cache skipped enum validation");
    rejects([&] { runtime.call("BadRef"); }, "Cached native calls skipped ref/out validation");
    runtime.bind("injected", 7); check(runtime.call("Dynamic").number() == 7, "Late binding did not create a slot");
    runtime.bind("injected", 8); check(runtime.call("Dynamic").number() == 8, "Rebinding retained a stale value");
    runtime.set("outer", 12); check(runtime.get("outer").number() == 12, "Runtime get/set lost slot updates");
    auto oldMethod = runtime.get("Sum");
    runtime.load("method Sum(n)\n return n * 2\nmend");
    eryscript::Arguments args; args.values = {4};
    check(oldMethod.callable()->function(args).isNull() && runtime.call("Sum", {4}).number() == 8,
        "Method caching changed reload/callback invalidation");

    Example::Views views;
    auto object = reflection.object(&views).object();
    auto first = object->get("first").object();
    check(first == object->get("first").object(), "A stable reflected member view was rebuilt");
    views.first.x = 11; check(first->get("x").number() == 11, "Cached member fields became snapshots");
    auto target = object->get("target").object();
    check(target == object->get("target").object(), "A stable reflected pointer view was rebuilt");
    views.target = &views.second;
    check(object->get("target").object()->get("x").number() == 3, "Pointer view cache retained its old target");
    views.target = nullptr; check(object->get("target").isNull(), "Pointer view cache ignored null");
    auto snapshot = object->get("points").array();
    snapshot->values[0] = reflection.object(&views.second);
    check(object->get("points").array() != snapshot, "A reflected collection stopped returning fresh snapshots");
    auto readOnly = reflection.object(&std::as_const(views)).object()->get("first").object();
    rejects([&] { readOnly->set("x", 1); }, "Cached views lost const protection");
}
void standalone() {
    Example::Counter counter;
    eryscript::Runtime runtime;
    eryscript::Reflection reflection;
    reflection.bindNamespace<^^Example>(runtime);
    constexpr auto sine = std::meta::reflect_function(*static_cast<double(*)(double)>(std::sin));
    constexpr auto squareRoot = std::meta::reflect_function(*static_cast<double(*)(double)>(std::sqrt));
    reflection.bindFunctions<sine, squareRoot>(runtime);
    runtime.load(R"ery(
using Example
var clicks = 0
method Check
    position.x = 3
    inherited = 20
    mode = Mode.Active
    Counter.limit = 80
    Example.setting = 5
    var made = new Counter(10)
    var defaults = new Point()
    var point = new Point(8, 9)
    var returned = makePoints()
    position.y = returned[0].y
    var flags = bits
    flags[0] = true
    bits = flags
    var number = 3
    increment(ref number)
    fill(out var filled)
    clicked = Clicked
    return add() + made.add() + Counter.twice(point.y) + choose(2) + number + filled + total(new double[2])
mend
method Clicked
    clicks = clicks + 1
mend
method Standard
    return sqrt(9) + sin(0)
mend
method ConstConstructor
    var value = new ReadOnly(7)
    return value.value + answer()
mend
method References
    var alias = identity(new Counter(23))
    var point = first(makePoints())
    fillPoint(out var output)
    return alias.value + point.y + output.y
mend
)ery", reflection.object(&counter));
    check(runtime.call("Check").number() == 59.5, "Automatic invocation/construction/ref/out failed");
    check(counter.position.x == 3 && counter.position.y == 5 && counter.bits[0] &&
        counter.inherited == 20 && counter.mode == Example::Mode::Active,
        "Automatic fields/inheritance/enums failed");
    check(Example::setting == 5 && Example::Counter::limit == 80, "Static/namespace field reflection failed");
    check(runtime.call("Standard").number() == 3, "Direct standard-library function reflection failed");
    check(runtime.call("References").number() == 35, "Reference result/out object lifetime failed");
    check(runtime.call("ConstConstructor").number() == 49, "Const aggregate/inherited override failed");
    rejects([&] { runtime.get("this").object()->get("secret"); }, "Private field was exposed");
    counter.clicked(); check(runtime.get("clicks").number() == 1, "Reflected callback failed");
    auto previous = counter.clicked;
    runtime.load("var clicks = 0", reflection.object(&counter));
    previous(); check(runtime.get("clicks").number() == 0, "Reload did not invalidate callback");
    runtime.load("method Change\n value = 1\nmend", reflection.object(&std::as_const(counter)));
    rejects([&] { runtime.call("Change"); }, "Const object was writable");
    runtime.load("method Bad\n value = 1.5\nmend", reflection.object(&counter));
    rejects([&] { runtime.call("Bad"); }, "Fractional integer assignment accepted");
    eryscript::Value expired;
    { eryscript::Reflection temporary; expired = temporary.object(&counter); }
    rejects([&] { expired.object()->get("value"); }, "Expired native context was accessed");
    eryscript::Runtime expiredRuntime;
    { eryscript::Reflection temporary; temporary.bindNamespace<^^Example>(expiredRuntime); }
    rejects([&] { expiredRuntime.load("using Example\n var counter = new Counter()"); }, "Expired constructor context was accessed");
}
void engine() {
    using namespace tiny3d;
    Scene scene;
    auto& entity = scene.entities.emplace_back();
    entity.addComponent<MeshRenderer>(); entity.addComponent<Camera>();
    auto& button = entity.addComponent<Button>();
    auto& script = entity.addComponent<EryScript>(R"ery(
using tiny3d
var awakes = 0
var updates = 0
var clicks = 0
method Awake
    awakes = awakes + 1
    entity.getComponent<MeshRenderer>().color = new Color(1, 2, 3)
    entity.getComponent<Camera>().depth = 9
    entity.getComponent<Button>().onClick = Clicked
    entity.addComponent<SphereCollider>().radius = 0.75
    entity.transform.localPosition.y = 2
    entity.transform.localRotation = Quaternion.fromEuler(new Vector3(0, 0.2, 0))
mend
method Update
    updates = updates + 1
    entity.transform.localPosition.x = entity.transform.localPosition.x + deltaTime
    if input.held(Key.W)
        entity.tag = "Keyboard"
    end
    if input.pressed(MouseButton.Left)
        entity.layer = 3
    end
mend
method Clicked
    clicks = clicks + 1
mend
method Broken
    entity.getComponent<Camera>().depth = "not a number"
mend
method WireBroken
    entity.getComponent<Button>().onClick = Broken
mend
method Equality
    if Key.Kana != Key.Hangul
        return false
    end
    var renderer = entity.getComponent<MeshRenderer>()
    var base = entity.getComponent<Component>()
    return entity == this.entity && entity.getComponent<Camera>() == entity.getComponent<tiny3d.Camera>() && renderer == base && base == renderer
mend
)ery");
    Scripts scripts;
    scripts.awake(scene);
    check(script.error().empty(), script.error().c_str());
    check(script.runtime().get("awakes").number() == 1 && entity.transform.localPosition.y == 2, "Awake/live field failed");
    check(entity.getComponent<Camera>()->depth == 9 && entity.getComponent<MeshRenderer>()->color.g == 2,
        "Reflected component lookup failed");
    check(entity.getComponent<SphereCollider>()->radius == .75f, "Reflected template component creation failed");
    check(script.runtime().call("Equality").boolean(), "Native identity/enum alias equality failed");
    Input input; input.set(Key::W, true); input.set(MouseButton::Left, true);
    scripts.update(scene, .25f, input);
    check(script.error().empty(), script.error().c_str());
    check(script.runtime().get("updates").number() == 1 && entity.transform.localPosition.x == .25f &&
        entity.tag == "Keyboard" && entity.layer == 3, "Update/input/overload failed");
    button.onClick(); check(script.runtime().get("clicks").number() == 1, "Engine callback failed");
    rejects([&] { script.runtime().get("input").object()->set("mouseX", 99); }, "Input snapshot was writable");
    auto oldCallback = button.onClick;
    script.restart(); oldCallback();
    check(script.runtime().get("clicks").number() == 1, "Restart did not invalidate callback");
    scripts.update(scene, 0, {});
    check(script.error().empty() && script.runtime().get("awakes").number() == 1, "Restart failed");
    auto camera = script.runtime().get("entity").object()->get("getComponent");
    eryscript::Arguments args; args.typeNames = {"Camera"};
    args.typeNames = {"Other.Camera"};
    rejects([&] { camera.callable()->function(args); }, "Wrong template namespace was accepted");
    args.typeNames = {"tiny3d.Camera"};
    auto savedCamera = camera.callable()->function(args);
    for (auto i = entity.components.begin(); i != entity.components.end(); ++i) {
        if (dynamic_cast<Camera*>(i->get())) { entity.components.erase(i); break; }
    }
    rejects([&] { savedCamera.object()->get("depth"); }, "Removed component was accessed");
    script.runtime().call("WireBroken"); button.onClick();
    check(!script.error().empty() && !script.runtime().hasMethod("Update"), "Callback failure was not recorded/stopped");
    auto savedEntity = script.runtime().get("entity");
    auto callback = button.onClick;
    scene.entities.clear(); callback();
    rejects([&] { savedEntity.object()->get("tag"); }, "Destroyed engine context was accessed");
    check(!input.held(Key::Count) && !input.pressed(MouseButton::Count), "Input bounds check failed");
}
} // namespace

int main() {
    try { standalone(); optimizedRuntime(); engine(); std::cout << "Reflection checks passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
