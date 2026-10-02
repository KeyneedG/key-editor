# EryScript for C++26

This directory is a standalone port of the language in EryScript.cs. It uses only
the C++ standard library. Its header and source never include Tiny3D or Unity.
The Tiny3D component and bindings live outside this directory, in
../include/tiny3d/eryscript.hpp and ../src/eryscript.cpp.

Build this directory directly with CMake, or add it with add_subdirectory and
link your application against the eryscript target:

~~~bash
cmake -S Engine/EryScript -B Engine/build/GCC/EryScript -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build Engine/build/GCC/EryScript
~~~

Run these commands from MSYS2 UCRT64 in the repository directory, using GCC 16+
and CMake 3.25+. Configuration verifies C++26 reflection, and `-freflection`
propagates to programs linking the `eryscript` CMake target. A standalone build
produces a library; there is no dependency on the editor solution or a game.
`include/eryscript/reflection.hpp` provides the automatic binding layer. It has
no engine includes and can be used independently of Tiny3D.

## Language

Scripts use one statement per line; // comments are supported. Statements can
also be separated by semicolons. Names are case-sensitive. Keywords are not.

- var name = expression declares a variable; var name initializes it to null.
- name = expression assigns an existing variable. Member/index assignments work.
- method Name(parameters) ... mend defines a method; parentheses are optional
  for a method without parameters. return expression and bare return work.
- if condition ... elseif condition ... else ... end supports nested branches.
- for i = first to last ... end counts upward with an inclusive upper bound.
  Each iteration has its own scope. break and continue affect the nearest loop.
- Literals are numbers, strings, true, false, null. Escapes include \n, \r, \t,
  \0, \", and \\. Operators include + - * /, comparisons, && and ||.
  Logical operators short-circuit. Unary ! and % are also supported.
- Method calls work with or without the original call prefix.
- new RegisteredType(arguments) uses a registered C++ constructor.
- new int[3], new float[2,3] and new float[2][3] create fixed arrays.
  Both a[i,j] and a[i][j] work for rectangular arrays. Arrays expose Length,
  Rank and GetLength(dimension). Indices and dimensions are checked.
- new List<int>() creates a resizable array with Add, Clear, Count and indexing.
  new Dictionary<string,int>() creates an object indexed by string keys.
- using Namespace resolves names within bound namespace objects.
  Namespace:Type.Member and Namespace.Type.Member both work.
- Explicit generic arguments are passed to C++ bindings as typeNames. Native
  calls support ref, out, out var name and in; modified ref/out values are
  written back to the original variable/member/index once the call returns.
- specify name = "Name", specify id = "id", specify enabled = true and
  specify readOnly = false supply top-level metadata. Values must be literals.
  Disabled scripts do not execute. Read-only scripts reject changed source.

Top-level variables persist between method calls. Method parameters and local
variables do not. Assignment searches the current scope and then parent scopes.
Script methods can be passed as callbacks.

Variable names are mapped to numbered slots when a script loads. Call and loop
storage is reused, with locals cleared between invocations and iterations.
Reflection reuses views of stable nested members and filters native overloads
by argument count and generic names. Value conversion, overload ambiguity,
const protection and lifetime checks still run on every call; collections
remain fresh array snapshots. The language syntax is unchanged.

Console.WriteLine(value), Debug.Log(value), Math.Sin/Cos/Tan/Abs/Sqrt/Floor/
Ceiling/Round/Min/Max/Clamp and Math.PI are built in. Mathf aliases Math.

## Standalone API

Include eryscript/eryscript.hpp. Runtime::load(source, optionalRoot) parses the
source once and executes its top-level statements once. hasMethod(name) checks
for a method; call(name, arguments) invokes it. get/set access global variables.
stop() also invalidates previously obtained script callbacks. Loading new source
resets script variables and invalidates old callbacks, while preserving bindings.

Runtime::bind(name, Value) exposes a C++ value or function. Functions receive
Arguments&, with values, modes, and typeNames. Use requireCount to validate
arity; inspect modes/typeNames when a binding needs a specific signature.
Runtime::constructor(name, Function) registers construction by name.
Object::fields stores values/functions. Object::property(name, getter, setter)
exposes live C++ properties; omitting the setter makes a property read-only.
See the examples in ../../DEMO/EryScriptExamples.md.

## Reflection API

Include `eryscript/reflection.hpp` and keep an `eryscript::Reflection` instance
alive for as long as its runtime uses native objects. Then call
`reflection.bindNamespace<^^YourNamespace>(runtime)` once. Public classes,
inherited fields, instance/static methods, constructors, enums and namespace
functions/variables are discovered from C++ declarations. Methods keep their
C++ names and signatures; default arguments and overload selection work.
Private members, destructors, assignment/conversion operators and variadic C
functions are not exported. Nested namespaces can be selected separately.

`reflection.object(&object)` supplies a live script root. A const pointer makes
the object read-only. Names are case-sensitive, just like C++. Nested reflected
fields remain live; C++ results returned by value own their storage. Numeric
conversions check range, finiteness and integral values. Enum arguments accept
reflected enum constants, declared enumerator names, or valid numeric values.
Typed constants distinguish enum overloads such as keyboard vs mouse input.
Strings and numeric values convert directly, standard sequences convert to/from
script arrays, and `std::function` supports native/script callbacks. Unique
ownership and noncopyable collection elements cannot be assigned from arrays.
Pointer/reference targets remain owned by the C++ host unless created by a
reflected constructor; the host must keep them alive. `validate` allows a host
to check that objects still exist, and `onError` reports callback exceptions.
Destroying Reflection invalidates its native views and callbacks.

For existing C++ functions, `reflection.bindFunctions<reflections...>(runtime)`
discovers names and signatures without writing binding lambdas. For example:

~~~cpp
constexpr auto sine = std::meta::reflect_function(
    *static_cast<double(*)(double)>(std::sin));
reflection.bindFunctions<sine>(runtime); // Script can call sin(number).
~~~

Select a compiled overload with its C++ signature; already-instantiated C++
templates can be selected the same way. Single-type member templates are
automatically instantiated for types declared in the selected namespace when
their constraints permit it. Constrain templates appropriately, as in ordinary
C++. Reflection runs at compile time: it cannot discover an unincluded header
or instantiate arbitrary templates requested later at runtime. C++ signatures
without a supported value conversion produce an error rather than an unsafe
cast. Script arrays still use the interpreter's own collection operations.

## Changes from the C# implementation

C++26 reflection replaces the engine's explicit member bindings with generated
native views and calls. CLR assembly discovery and arbitrary runtime generic
instantiation remain different: select C++ APIs during compilation. Unity objects,
coroutines, ScriptDebugger, ModsManager subscriptions, and Unity/JSON AST
serialization were removed. Source strings are the saved script representation.
Tiny3D supplies scene lifetime checks and script execution hooks separately.
The language's primitive constructors, collections, Math and Console remain
built-in interpreter facilities; engine members use the generic reflection layer.

Numbers use double, including integer literals. Arrays/collections hold dynamic
values; generic collection names do not enforce CLR element types. Primitive
numeric arrays start at zero, bool arrays at false, and object arrays at null.
Strings use UTF-8 bytes; indexing and Length count bytes.
Objects and arrays use shared ownership; there is no cycle-collecting garbage collector.
Default script IDs are stable per Runtime, rather than generated GUIDs.
Malformed statements produce errors with line numbers instead of being skipped.
Loop control propagates through nested branches correctly.

Runtime::stepLimit defaults to 100000 interpreter steps per run/call. Method
recursion is limited to 32 calls, with separate bounds for expression and block
depth. An invalid script throws Error or a standard exception. Native callers
decide how to report it; the Tiny3D component records it and stops that script.
