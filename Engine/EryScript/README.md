# EryScript for C++17

This directory is a standalone port of the language in EryScript.cs. It uses only
the C++ standard library. Its header and source never include Tiny3D or Unity.
The Tiny3D component and bindings live outside this directory, in
../include/tiny3d/eryscript.hpp and ../src/eryscript.cpp.

Build this directory directly with CMake, or add it with add_subdirectory and
link your application against the eryscript target:

~~~powershell
cmake -S Engine/EryScript -B Engine/build/EryScript -G Ninja
cmake --build Engine/build/EryScript
~~~

Run these commands from Developer PowerShell for VS 2022. A standalone build
produces a library; there is no dependency on the editor solution or a game.

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

## Changes from the C# implementation

C++17 has no CLR reflection. Explicit bindings replace assembly discovery,
automatic overload resolution, generic CLR instantiation, and reflection-based
member access. Only registered types and members are available. Unity objects,
coroutines, ScriptDebugger, ModsManager subscriptions, and Unity/JSON AST
serialization were removed. Source strings are the saved script representation.
Tiny3D adapts logging, transforms, input, components, and callbacks separately.

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
