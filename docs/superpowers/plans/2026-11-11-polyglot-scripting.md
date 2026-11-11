# Polyglot Scripting (JavaScript + Lua) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** A binding declared once in C++ is reachable from JavaScript and from Lua, on client and server, and every builtin JavaScript scripts use today is exposed to Lua under the same name.

**Architecture:** `Framework::Scripting::Bind` records a neutral description of each API plus per-language thunks. The JavaScript backend materialises through v8pp (so the existing catalog, `GetClass(isolate)` and project bindings keep working); the Lua backend builds userdata, metatables and global tables in one `lua_State` per resource. Dynamic builtins are written against `Bind::Call`/`Bind::Value`, script functions kept beyond a call are `ScriptCallable`, and values cross languages as `TransferredValue`.

**Tech Stack:** C++23, PUC Lua 5.5.1 (vcpkg `lua[cpp]`), V8/libnode, v8pp (MafiaHub fork), nlohmann-json, function2, CMake, zpl unit (`code/tests/unit.h`).

**Spec:** `docs/superpowers/specs/2026-11-11-polyglot-scripting-design.md` — read it; it defines the interfaces this plan names (`Bind::Call`, `Bind::Value`, `ScriptCallable`, `Deferred`, the value-mapping table, the sandbox profile).

## Global Constraints

- **Build and test (macOS/Linux host):** `cmake -B build` once, then `cmake --build build --target FrameworkTests FrameworkClientScriptingTests -j 10`. Run `build/bin/FrameworkTests` (from `build/bin`) and `build/bin/FrameworkClientScriptingTests`. `FW_TEST_FILTER=<substr,substr>` runs only matching modules. Never create another build directory. The baseline is **841 tests / 57 modules** and **4 tests / 1 module**, all passing; a task may only add to these.
- **TDD, every task:** write the failing test first, run it and see it fail for the expected reason, implement, run the focused module green, run both full suites green, run `scripts/format_codebase.sh` scope rules on the files you touched (clang-format, `.clang-format` at the root), commit. Record the red and green output in the task report.
- **Test layout:** one header per module in `code/tests/modules/<name>_ut.h` using `MODULE(name, { IT("...", { EQUALS(a, b); }); })`; include it and add `UNIT_MODULE(name);` in `code/tests/framework_ut.cpp` (or `client_scripting_ut.cpp` for client-runtime tests). Tests that create a `NodeEngine` follow `js_features_ut.h` (one engine per `IT`, V8 scopes around registration).
- **Sources are listed, not globbed:** add every new `.cpp` to the right list in `code/framework/CMakeLists.txt` (`FRAMEWORK_SRC` shared, `FRAMEWORK_SERVER_SRC`, `FRAMEWORK_CLIENT_SRC`).
- **Style:** file header licence comment as in neighbouring files (year 2026); `#pragma once`; `namespace Framework::Scripting::Bind {` closed with `} // namespace Framework::Scripting::Bind`; private members `_camelCase`; classes PascalCase and `final` where possible; comments only where behaviour is not obvious, matching the density and plain tone of the surrounding code.
- **C++:** only what MSVC 2022, GCC 13 and Apple clang all have (no deducing `this`, `std::print`, `std::ranges::to`, `std::flat_map`, `std::move_only_function`, `std::generator`, `std::stacktrace`). No C++23 library types (`std::expected`) in headers; `Utils::Result` is the error type.
- **Lua headers:** include `<lua.h>`, `<lauxlib.h>`, `<lualib.h>` directly; never `lua.hpp`. Lua is built as C++: a Lua error is a C++ exception. Never call `lua_error`/`luaL_error` inside a `catch` block or while a non-trivial C++ object is alive in the frame that must survive; catch `std::exception` only.
- **No behaviour change for JavaScript.** Public C++ signatures used by projects stay: `Engine`, `GetIsolate()`, `GetContext()`, `T::GetClass(isolate)`, `T::Register(isolate, ...)`, `T::NewInstance`, `T::UnregisterIsolate`, `Events::EmitReserved*`, `Builtins::RegisterHandleTransfer<T>`, `MergeScriptingCatalog`. The v8pp catalog content of a ported builtin is unchanged (golden test, Task 6).
- **Naming parity:** a Lua global, function, method and property has exactly the JavaScript name. Lua instance methods are called with `:`, statics and namespace functions with `.`.
- **Error wording:** `"<Api>.<method>: <what was expected / what went wrong>"`, per `scripting/builtins/README.md`.
- **Never** write outside the repository; never null-check a resolved pattern address (AGENTS.md).
- **Commits:** `Module: Brief description`, wrapped at 72 characters, one atomic commit per logical change.

## Review Focus

- A Lua script passing the wrong type, too many or too few arguments to any bound function must get a Lua error naming the function, never a crash or a silent default. (Tasks 3, 4, 12)
- A resource stopped while its Lua coroutines are suspended (timer, pending async call, event in flight) must not be resumed afterwards and must not leak its state. (Tasks 9, 10, 14, 19)
- A C++ exception thrown by a bound implementation while called from Lua must surface as a Lua error with the state still usable. (Task 4)
- A value that cannot cross (cycle, coroutine, invalid UTF-8, unbound userdata) must be refused with the path to it, in both directions. (Task 13)
- A hostile client script (bytecode chunk, `load` of binary, runaway loop, allocation bomb, `require` outside the resource) must be stopped by the sandbox. (Tasks 8, 10)

---

## File Structure

```
code/framework/src/scripting/
  script_engine.h                 neutral engine base (Task 10)
  bind/
    types.h, doc.h, descriptors.h, descriptors.cpp     neutral description (Task 2)
    lua_convert.h                                      Lua stack converters (Task 3)
    lua_class.h, lua_class.cpp                         non-template Lua class machinery (Task 4)
    class.h                                            Bind::Class<T> (Tasks 4, 5, 12)
    v8_backend.h                                       v8pp materialisation helpers (Task 5)
    value.h, call.h, lua_call.cpp, v8_call.cpp         dynamic interface (Task 12)
    namespace.h, namespace.cpp                         Bind::Namespace (Task 12)
    callable.h, lua_callable.cpp, v8_callable.cpp      ScriptCallable, Deferred (Task 14)
  lua/
    lua_runtime.h, lua_runtime.cpp, lua_sandbox.cpp    one state (Task 8)
    lua_scheduler.h, lua_scheduler.cpp                 coroutines and timers (Task 9)
    lua_engine.h, lua_engine.cpp                       engine (Task 10)
    lua_transfer.h, lua_transfer.cpp                   TransferredValue <-> Lua (Task 13)
    lua_json.h, lua_json.cpp                           JSON codec (Task 13)
code/tests/modules/lua_test_helpers.h                  shared Lua test helpers (Task 3)
code/tests/fixtures/                                   golden catalog (Task 6)
```

---

### Task 1: Lua 5.5 dependency

**Files:**
- Modify: `vcpkg.json`, `vendors/CMakeLists.txt`, `code/framework/CMakeLists.txt`, `LICENSE.txt`, `NOTICE.txt` (if it lists third-party notices)
- Create: `code/tests/modules/lua_smoke_ut.h`; Modify: `code/tests/framework_ut.cpp`

**Interfaces:**
- Produces: CMake target `unofficial::lua::lua-cpp` linked `PUBLIC` into `Framework`, so `<lua.h>` resolves in every Framework and test translation unit and in an installed SDK.

**Requirements:**
- `vcpkg.json`: add `{ "name": "lua", "features": ["cpp"], "$comment": "..." }` saying why `cpp`: errors must be C++ exceptions so they unwind through bindings.
- `vendors/CMakeLists.txt`: `find_package(unofficial-lua CONFIG REQUIRED)` beside the other vcpkg packages; link the target to `Framework` in `code/framework/CMakeLists.txt`. Make sure the SDK install/export path that ships the other vcpkg dependencies also covers it (follow how `glm`/`fmt` are propagated; `cmake/FrameworkInstall.cmake`, `cmake/FrameworkConfig.cmake.in`).
- `LICENSE.txt` third-party list: `lua             - https://www.lua.org/license.html`.

**Tests (`lua_smoke`):**
- `lua_newstate` with a `malloc`-backed allocator, `luaL_openselectedlibs(L, LUA_GLIBK, 0)`, load `"return 1 + 1"` with mode `"t"`, `lua_pcall` → integer `2`.
- `LUA_VERSION_NUM == 505`.
- A C++ exception thrown from a `lua_CFunction` under `lua_pcall` is reported as an error status and a local object's destructor in a frame between has run (proves C++ linkage with exceptions): push a C function that constructs an object with a flag-setting destructor then calls `luaL_error`; assert the flag is set and `lua_pcall` returned `LUA_ERRRUN`.

**Done when:** both suites pass, `cmake -B build` succeeds from the existing tree.

---

### Task 2: Neutral description — types, docs, descriptors

**Files:**
- Create: `code/framework/src/scripting/bind/types.h`, `doc.h`, `descriptors.h`, `descriptors.cpp`
- Test: `code/tests/modules/bind_types_ut.h`

**Interfaces (Produces), all in `Framework::Scripting::Bind`:**

```cpp
// types.h
enum class TypeKind : uint8_t { Void, Boolean, Integer, Number, String, Optional, Array, Class, This, Any, Custom };
struct TypeRef { TypeKind kind = TypeKind::Void; std::string name; std::vector<TypeRef> args; std::string typescript; std::string lua; };
namespace Type { TypeRef Custom(std::string typescript, std::string lua); TypeRef Class(std::string name); TypeRef This(); TypeRef Any(); }
template <typename T> struct ScriptClassName;                 // specialised by FW_SCRIPT_CLASS
#define FW_SCRIPT_CLASS(CppType, Name)                        // at global namespace scope; gives ScriptClassName<CppType>::value
template <typename T> TypeRef TypeOf();                       // cv/ref/pointer stripped; table in spec §1.2
std::string ToTypeScript(const TypeRef &type);
std::string ToLuaAnnotation(const TypeRef &type);

// doc.h
enum class Side : uint8_t { Shared, Server, Client };
bool Materialises(Side member, Side runtime);                 // Shared always; otherwise equal
class Doc final {
  public:
    explicit Doc(std::string summary);
    Doc &Param(std::string name, std::string description);
    Doc &OptionalParam(std::string name, std::string description);  // documented optional whatever the C++ type
    Doc &ParamType(TypeRef type);                                   // overrides the last Param's type
    Doc &Returns(std::string description);
    Doc &ReturnType(TypeRef type);
    // accessors used by descriptors
};

// descriptors.h
struct ParamDesc { std::string name; TypeRef type; bool optional = false; std::string description; };
struct FunctionDesc { std::string name; std::string description; std::vector<ParamDesc> params; TypeRef returns; std::string returnsDescription; bool isStatic = false; Side side = Side::Shared; };
struct PropertyDesc { std::string name; TypeRef type; std::string description; bool readonly = false; bool isStatic = false; Side side = Side::Shared; };
struct ClassDesc { std::string name; std::string description; std::vector<std::string> bases; bool isNamespace = false; std::optional<FunctionDesc> constructor; std::vector<FunctionDesc> functions; std::vector<PropertyDesc> properties; };
template <typename F> FunctionDesc DescribeFunction(std::string name, const Doc &doc, bool isStatic, Side side);
template <typename... Args> FunctionDesc DescribeConstructor(const Doc &doc);
class ApiCatalog final { public: static ApiCatalog &Get(); void Record(const ClassDesc *desc); const std::vector<const ClassDesc *> &Entries() const; const ClassDesc *Find(std::string_view name) const; };
```

**Requirements:**
- `ToTypeScript`: `void`, `boolean`, `number` (Integer and Number), `string`, `T | undefined`, `T[]`, class name, `this`, `any`, the custom spelling. `ToLuaAnnotation`: `nil`, `boolean`, `integer`, `number`, `string`, `T?`, `T[]`, class name, `self`, `any`, the custom spelling.
- `TypeOf<glm::vec2/vec3/vec4/quat>()` is `Class("Vector2"/"Vector3"/"Vector4"/"Quaternion")`. `TypeOf<T>()` for a class without `ScriptClassName` is a compile error naming `FW_SCRIPT_CLASS`.
- `DescribeFunction<F>` takes parameter types from `F` (member function: receiver excluded; free function/lambda: all parameters), names and descriptions from `Doc` in order. A `std::optional<T>` parameter is `optional` with type `T`. A member function of `C` returning `C &` has return type `This`. A `Doc` naming a different number of parameters than `F` has throws `std::logic_error` naming the function.
- `ApiCatalog::Record` ignores a description already recorded (same pointer).

**Tests (`bind_types`):** one `IT` per row of spec §1.2 for `TypeOf`+both spellings; optional parameter flagged and unwrapped; `T &` return of own class is `this`/`self`; `Doc` count mismatch throws; `ParamType(Type::Custom("{ x: number }", "{ x: number }"))` overrides; `Materialises` truth table; `ApiCatalog::Find`.

---

### Task 3: Lua stack converters and the Lua test helper

**Files:**
- Create: `code/framework/src/scripting/bind/lua_convert.h`, `code/tests/modules/lua_test_helpers.h`
- Test: `code/tests/modules/lua_convert_ut.h`

**Interfaces:**
- Consumes: Task 1 (Lua), Task 2 (`TypeOf`).
- Produces:

```cpp
namespace Framework::Scripting::Bind {
    // Specialise for a C++ type to teach Lua thunks about it.
    template <typename T, typename Enable = void> struct LuaConvert;
    //   static bool Is(lua_State *L, int index);
    //   static T Get(lua_State *L, int index);            // precondition: Is()
    //   static void Push(lua_State *L, const T &value);   // pushes exactly one value
    //   static const char *Expected();                    // "number", "string", ...
    const char *LuaTypeNameAt(lua_State *L, int index);   // luaL_typename, or the bound class name of a userdata
}

// lua_test_helpers.h
namespace LuaTest {
    struct State final { lua_State *L; State(); ~State(); };           // base, coroutine, table, string, math, utf8
    // Runs `source` in protected mode. The first result as text ("nil", "true", "2", "1.5", the string itself),
    // or "<error> " followed by the message.
    std::string Eval(lua_State *L, const std::string &source);
    double EvalNumber(lua_State *L, const std::string &source);        // NaN when it failed or is not a number
}
```

**Requirements (spec §1.2, "Lua runtime value" column):** `bool` (boolean only); integral types (integer, or float with integral value via `lua_tointegerx`; out-of-range for the C++ type is not `Is`); `uint64_t` as two's-complement; `float`/`double` (`lua_type == LUA_TNUMBER`, no strings); `std::string`, `std::string_view` (valid only for the call), `const char *` (`lua_type == LUA_TSTRING` only); `std::optional<T>` (`nil`/none ↔ `nullopt`); `std::vector<T>` (table with keys `1..n`; pushes a sequence); enums as their underlying integer. No class support yet (Task 4 adds it).

**Tests (`lua_convert`):** round-trip each type through a pushed C closure; `Is` rejects: string for number, `"5"` for integer, `1.5` for integer, number for string, `0` for bool, `300` for `uint8_t`; `uint64_t` `0xFFFFFFFFFFFFFFFF` round-trips; optional from `nil` and from absent index; vector `{1,2,3}` and rejection of `{1, nil, 3}` and of `{a=1}`; `LuaTest::Eval` formats and reports errors.

---

### Task 4: `Bind::Class<T>` and the Lua backend

**Files:**
- Create: `code/framework/src/scripting/bind/class.h`, `lua_class.h`, `lua_class.cpp`
- Modify: `bind/lua_convert.h` (bound-class conversion), `code/framework/CMakeLists.txt`
- Test: `code/tests/modules/bind_class_lua_ut.h`

**Interfaces:**
- Consumes: Tasks 2, 3.
- Produces:

```cpp
enum class LuaMetaEvent : uint8_t { Add, Sub, Mul, Div, Unm, Eq, Lt, Le, Len, Concat };

template <typename T>
class Class final {
  public:
    Class(std::string name, std::string description);            // records itself in ApiCatalog; one definition per T
    template <typename Base> Class &Inherit();                    // Base must have a definition created earlier
    template <typename... Args> Class &Constructor(Doc doc);     // T(Args...)
    template <typename F> Class &Method(std::string name, F function, Doc doc, Side side = Side::Shared);
    template <typename F> Class &Static(std::string name, F function, Doc doc, Side side = Side::Shared);
    template <typename G> Class &Property(std::string name, G getter, std::string description, Side side = Side::Shared);              // read-only
    template <typename G, typename S> Class &Property(std::string name, G getter, S setter, std::string description, Side side = Side::Shared);
    template <typename F> Class &LuaMeta(LuaMetaEvent event, F function);
    Class &LuaHook(fu2::function<void(lua_State *, int metatableIndex, int classTableIndex) const> hook);
    const ClassDesc &Describe() const;

    void MaterialiseLua(lua_State *L, Side runtime) const;       // idempotent per state; creates the metatable and the class table
    void PublishLua(lua_State *L, int targetIndex) const;        // target[name] = class table
    static const Class *Find();                                   // the definition for T, or nullptr
};
// LuaConvert for a bound class: T (copy out), T & / const T & / T * (into the userdata), Push(L, T) creates a userdata by value.
```

**Requirements (spec §1.3):** userdata by value; registry metatable keyed by the definition's address; `__index` methods then getters, unknown key `nil`; `__newindex` setters, unknown/read-only raises `attempt to set '<key>' on <Class>`; `toString` method doubles as `__tostring`; `__gc` only when `!std::is_trivially_destructible_v<T>`; `__metatable = false`; `__name = <Class>`; class table with statics, `new`, `__call`, read-only; a method returning a reference to its own receiver returns the receiver userdata itself; inheritance with stored upcast, base parameters accept derived instances; argument errors `bad argument #n to '<Class>:<method>' (<expected> expected, got <actual>)` and `wrong number of arguments to '<Class>:<method>' (<min>..<max> expected, got <n>)` (too many is an error too; trailing optionals may be omitted); C++ `std::exception` → Lua error with `what()`, raised outside the catch block; members filtered by `Materialises(member.side, runtime)`.

**Tests (`bind_class_lua`)** with a test-local `Counter` (int field, `add(int) -> Counter &`, `get() const`, `value` rw property, `doubled` ro property, static `make(int)`, `toString`, `throws()` throwing `std::runtime_error("boom")`, a `Server`-side method `kick`) and `SubCounter : Counter`:
- `Counter.new(2):add(3):get() == 5`; `Counter(2).value == 2`; chaining returns the same userdata (`rawequal(c, c:add(1))`).
- property write `c.value = 9`; `c.doubled = 1` raises; `c.nope == nil`; `c.nope = 1` raises.
- `Counter.make(4):get() == 4`; `tostring(c)` uses `toString`.
- wrong type, missing and extra arguments raise the documented messages; `c.add(5, 1)` (no receiver) raises a receiver error.
- `c:throws()` raises `boom` and the state still evaluates `1 + 1`.
- `LuaMeta(Add)` gives `(a + b):get()`; `Eq` gives `a == b`.
- `SubCounter` instance calls `add`, passes where `Counter` is expected; a `Counter` is refused where `SubCounter` is expected.
- `kick` is absent in a `Side::Client` state and present in a `Side::Server` state.
- `getmetatable(c) == false`; `Counter.new = nil` raises.
- a non-trivially-destructible bound type's destructor runs on `collectgarbage` (count with a static).

---

### Task 5: V8 backend for `Bind::Class<T>`

**Files:**
- Create: `code/framework/src/scripting/bind/v8_backend.h`
- Modify: `bind/class.h`
- Test: `code/tests/modules/bind_class_v8_ut.h`

**Interfaces:**
- Consumes: Task 4, v8pp (`build/_deps/v8pp-src/v8pp/class.hpp`, `metadata.hpp`).
- Produces on `Class<T>`:

```cpp
std::unique_ptr<v8pp::class_<T>> MaterialiseV8(v8::Isolate *isolate, v8pp::metadata::registry &catalog, Side runtime) const;
Class &V8Hook(fu2::function<void(v8pp::class_<T> &, v8::Isolate *) const> hook);   // language-specific members (toJSON), run after the declared ones
Class &V8Constructor(fu2::function<T *(const v8::FunctionCallbackInfo<v8::Value> &) const> factory);  // replaces v8pp's default factory, keeps the Constructor docs
```

**Requirements:**
- Reproduce what the hand-written builtins do today so the catalog and behaviour are identical: `v8pp::class_<T>(isolate, catalog, name, description)`, `auto_wrap_objects(true)`, `inherit<Base>()` after the base's class exists for the isolate, `ctor<Args...>(docs)`, `function(name, fn, docs)`, `property(name, getter[, setter], property_docs/property_options)` (read-only uses `property_options{description, type, true, false}`), statics via `js_function_template()->Set(isolate, name, v8pp::wrap_function_template(isolate, fn))` plus `catalog.constructor(name).record(v8pp::metadata::function_of<F>(name, docs, true))`.
- `v8pp::metadata::docs(returnType, {param(name, type, optional, description)...}, description, returnsDescription)` is derived from the `FunctionDesc` with `ToTypeScript`.
- Members filtered by side as in Task 4. `Inherit<Base>()` needs the base's `v8pp::class_`: the definition stores a `fu2::function<void(v8::Isolate *)>` "ensure base" that the caller's own `GetClass` supplies — add `Class &V8EnsureBase(fu2::function<void(v8::Isolate *) const>)`.

**Tests (`bind_class_v8`)** with the same `Counter`/`SubCounter` in a `NodeEngine`: the JS expressions mirroring Task 4's Lua ones (`new Counter(2).add(3).get() === 5`, property read/write, read-only property write is ignored, static, `String(c)` is unaffected unless `toString` is called, `throws()` throws `boom`, inheritance, side filter); the catalog (`v8pp::metadata::to_json`) contains the class with its description, the `add` function with parameter `amount: number`, return `this`, the static `make` flagged static, the read-only property flagged read-only.

---

### Task 6: Golden catalog of the existing builtins

**Files:**
- Create: `code/tests/modules/scripting_catalog_golden_ut.h`, `code/tests/fixtures/scripting_catalog_server.json`
- Modify: `code/tests/CMakeLists.txt` (compile definition `FW_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"`), `code/tests/framework_ut.cpp`

**Requirements:**
- The test builds a `NodeEngine` and a `ResourceManager`, sets the isolate's catalog to a fresh name (`SetScriptingCatalog(isolate, "golden-server")`, environment server) and registers everything `ServerScriptingModule::RegisterFrameworkBindings` registers (value types, `Events`, `Messages`, `Imports`, `Exports`, `Console`, `ExecutionEnvironment`, `Chat`, server `Voice`, `PendingConnection`, event and timer metadata) plus `Entity`, `Player`, `TextLabel`, `StateBag` (`GetClass`).
- It serialises the catalog with `v8pp::metadata::to_json`, parses it with nlohmann-json, **normalises** (symbols sorted by name; each symbol's functions sorted by `(static, name)`, properties by `(static, name)`; variables and global functions sorted by name) and compares with the fixture, reporting the first differing symbol name on failure.
- `FW_UPDATE_GOLDEN=1` in the environment rewrites the fixture instead of comparing. Generate and commit the fixture from the unported code.

**Tests (`scripting_catalog_golden`):** `IT("the server catalog matches the committed fixture")`; `IT("the fixture names every builtin")` asserting the fixture has symbols `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Color`, `Entity`, `Player`, `TextLabel`, `StateBag`, `Events`, `Messages`, `Exports`, `Imports`, `Chat`, `Voice` (use the names the catalog really has; list them in the report).

---

### Task 7: Port the value types

**Files:**
- Modify: `code/framework/src/scripting/builtins/{vector2,vector3,vector4,quaternion,color}.{h,cpp}`, `builtins.h`, `builtins.cpp`, `README.md` (registration section)
- Modify: `bind/lua_convert.h` or a new `scripting/bind/lua_glm.h` (glm converters)
- Test: `code/tests/modules/value_types_parity_ut.h`

**Interfaces:**
- Consumes: Tasks 4-6.
- Produces: for each `T` in `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Color`: `static const Bind::Class<T> &Definition();` and `static void RegisterLua(lua_State *L);` (materialise for the state's side and publish as a global); `Builtins::RegisterValueTypesLua(lua_State *L, Bind::Side side)`; `LuaConvert<glm::vec2/vec3/vec4/quat>` mapping to the wrapper classes like `scripting/v8pp_types.h` does for V8 (Lua accepts the userdata only, not a plain table); `FW_SCRIPT_CLASS` for the five classes.

**Requirements:**
- `GetClass(isolate)` builds from `Definition().MaterialiseV8(...)`; every existing public member keeps its signature and behaviour. `toJSON` stays a `V8Hook` with its catalog record; `Color`'s lenient constructor stays through `V8Constructor`.
- The golden test stays green with no fixture change.
- Lua additions through `LuaMeta`, non-mutating, returning new values: vectors `+`, `-`, `*` (vector × number and number × vector), `/` (vector / number), unary `-`, `==`; `Quaternion` `*` (quaternion × quaternion), `==`; `Color` `==`.
- Value types cross `ValueTransfer` as before (untouched).

**Tests (`value_types_parity`):** a table of `{ js, lua, expected }` evaluated in a `NodeEngine` and in a `LuaTest::State`, numbers compared as doubles with `1e-5` tolerance, strings exactly. At least: construction and each component; every method of each class (`add`, `sub`, `mul`, `div`, `dot`, `cross`, `normalize`, `lerp`, `set`, `distance`, `clone`, `length`, `lengthSquared`, quaternion and color specifics, statics); `toString()` text equal in both; chaining identity; and Lua-only operator cases. Existing `js_features` value-type tests stay green.

---

### Task 8: `LuaRuntime` — one sandboxed state

**Files:**
- Create: `code/framework/src/scripting/lua/lua_runtime.h`, `lua_runtime.cpp`, `lua_sandbox.cpp`
- Test: `code/tests/modules/lua_runtime_ut.h`

**Interfaces (Produces), `Framework::Scripting`:**

```cpp
struct LuaRuntimeOptions {
    std::string resourceName;
    Bind::Side side = Bind::Side::Server;
    size_t memoryLimitBytes = 0;                       // 0: no cap
    std::chrono::milliseconds entryDeadline {0};       // 0: off
    std::string rootPath;                              // resource directory, for chunk names and require (Task 10)
};
class LuaRuntime final {
  public:
    explicit LuaRuntime(LuaRuntimeOptions options);
    ~LuaRuntime();
    lua_State *State() const;
    static LuaRuntime *From(lua_State *L);             // any thread of the state; via lua_getextraspace of the main thread
    const LuaRuntimeOptions &Options() const;
    // Compiles `source` as text and runs it. False with message and traceback in `error`.
    bool RunString(std::string_view source, std::string_view chunkName, std::string &error);
    // Protected call of the function and `nargs` arguments on top of the main stack, under the deadline.
    // Leaves `nresults` results, or returns false with the error text popped into `error`.
    bool Call(int nargs, int nresults, std::string &error);
    size_t MemoryInUse() const;
    void SetPrintHandler(fu2::function<void(const std::string &line) const> handler);   // default: scripting logger, "[resource] line"
};
```

**Requirements (spec §2):** the library allow-list and removals exactly as specified; `load(chunk, name, mode, env)` accepts only string chunks and forces text mode; a binary chunk given to `RunString` or `load` is refused; allocator cap (an allocation that would exceed it returns `NULL`, Lua raises its memory error, the state stays usable after collection); count hook every 10 000 instructions raising `script ran too long` past the deadline, the deadline being armed per outermost `RunString`/`Call`; traceback message handler; `print` joins its arguments with tabs through `luaL_tolstring`.

**Tests (`lua_runtime`):** `RunString("x = 1 + 1")` then read `x`; syntax error and runtime error return false with chunk name and line; `io`, `debug`, `package`, `require` (until Task 10), `dofile`, `loadfile`, `string.dump`, `os.execute`, `os.getenv`, `os.exit`, `os.remove` are `nil`; `os.time()` is a number; `load("return 1")()` works; `load` of a function chunk and of `"\27Lua..."` is refused; `RunString` of a binary chunk is refused; with a 1 MB cap `local t = {} for i = 1, 1e7 do t[i] = i end` fails with a memory error and a following `RunString("return 1")` succeeds; with a 50 ms deadline `while true do end` fails with `script ran too long` within 2 s; `print("a", 1)` reaches the handler as `a\t1`; `LuaRuntime::From` on a coroutine thread returns the runtime; `collectgarbage("count")` works and `collectgarbage("stop")` raises.

---

### Task 9: Lua scheduler — coroutines and timers

**Files:**
- Create: `code/framework/src/scripting/lua/lua_scheduler.h`, `lua_scheduler.cpp`
- Modify: `lua_runtime.{h,cpp}`
- Test: `code/tests/modules/lua_scheduler_ut.h`

**Interfaces (Produces):**

```cpp
class LuaScheduler final {
  public:
    explicit LuaScheduler(LuaRuntime &runtime);
    // Runs the function and `nargs` arguments on top of the main stack in a new coroutine, to its end or its first suspension.
    // `done` runs once when the coroutine finishes (now or later): ok with its first result left on top of `thread`'s stack, or the error text.
    using Finished = fu2::unique_function<void(bool ok, lua_State *thread, const std::string &error)>;
    void Start(int nargs, Finished done = {});
    // Suspends the running coroutine; returns a token. Resume pushes nothing itself: `push` pushes the resume values and returns their count.
    uint64_t Suspend(lua_State *thread);               // call from a C function, then `return lua_yield(thread, 0)`
    void Resume(uint64_t token, fu2::unique_function<int(lua_State *)> push);
    void ResumeWithError(uint64_t token, const std::string &message);
    void Tick(std::chrono::steady_clock::time_point now);   // fires due timers
    size_t PendingTimers() const;  size_t SuspendedCoroutines() const;
    void SetErrorHandler(fu2::function<void(const std::string &message) const> handler);  // an uncaught error in a coroutine nobody waits on
};
// On LuaRuntime: LuaScheduler &Scheduler(); void Tick(std::chrono::steady_clock::time_point now);
// LuaRuntime::RunString runs the chunk through Scheduler().Start, so top-level code may sleep.
```

Lua globals installed by the runtime: `setTimeout(fn, ms, ...) -> id`, `setInterval(fn, ms, ...) -> id`, `clearTimeout(id)`, `clearInterval(id)`, `sleep(ms)`, `spawn(fn, ...)`. Limit 10 000 live timers per state (as `V8Engine::kMaxTimers`), exceeding raises.

**Requirements:** timers fire in due order from `Tick`, each callback in its own coroutine with the extra arguments; an interval re-arms from its due time; clearing inside its own callback is safe; `sleep` outside a coroutine cannot happen (every entry is one) — a non-yieldable context raises `sleep: cannot wait here`; an error in a timer callback goes to the error handler with a traceback and the timer keeps its schedule (interval) ; destroying the runtime with suspended coroutines and pending timers frees everything (no resume afterwards); the deadline of Task 8 applies to each resume.

**Tests (`lua_scheduler`)** driving `Tick` with synthetic time points: ordering of two timeouts; arguments passed; interval fires three times then is cleared from inside; `sleep(50)` resumes only after 50 ms of ticks and the code after it runs; `spawn` runs immediately up to its first suspension; `Start`'s `done` reports the first result for a synchronous function and, later, for one that slept; an error after a sleep reaches the error handler; `Suspend`/`Resume` round trip with values; `ResumeWithError` raises inside the coroutine and `pcall` there catches it; runtime destroyed with 3 sleeping coroutines and 2 timers: no crash, handler not called; timer limit.

---

### Task 10: `ScriptEngine` base and `LuaEngine`

**Files:**
- Create: `code/framework/src/scripting/script_engine.h`, `lua/lua_engine.h`, `lua/lua_engine.cpp`
- Modify: `code/framework/src/scripting/engine.h` (derive from `ScriptEngine`; move `_lastError`, `_resourceManager` and their accessors to the base), `lua/lua_runtime.cpp` (`require`)
- Test: `code/tests/modules/lua_engine_ut.h`

**Interfaces (Produces):**

```cpp
enum class Language : uint8_t { JavaScript, Lua };
class ScriptEngine : public Framework::Lifecycle {
  public:
    virtual Language GetLanguage() const = 0;
    virtual bool HandlesFile(std::string_view path) const = 0;
    virtual void Tick() = 0;
    virtual bool CreateResourceRuntime(const std::string &resourceName) = 0;
    virtual void DestroyResourceRuntime(const std::string &resourceName) = 0;
    virtual bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) = 0;
    const std::string &GetLastError() const;
    void SetResourceManager(ResourceManager *manager);  ResourceManager *GetResourceManager() const;
  protected:
    std::string _lastError;  ResourceManager *_resourceManager = nullptr;
};
// Engine (JavaScript): GetLanguage() == JavaScript; HandlesFile() true for every path not ending in ".lua". Everything else unchanged.

struct LuaEngineOptions { Bind::Side side = Bind::Side::Server; size_t memoryLimitBytes = 512u << 20; std::chrono::milliseconds entryDeadline {0}; };
class LuaEngine final : public ScriptEngine {
  public:
    explicit LuaEngine(LuaEngineOptions options = {});
    // Lifecycle Init/Shutdown; Tick ticks every runtime.
    using RuntimeSetup = fu2::function<void(LuaRuntime &) const>;
    void SetRuntimeSetupCallback(RuntimeSetup setup);          // installs bindings in every state created
    void SetResourceRoot(const std::string &resourceName, const std::string &rootPath);  // set by ResourceManager before execution
    LuaRuntime *GetRuntime(const std::string &resourceName) const;
    struct PendingError { std::string resourceName; std::string errorMessage; };
    std::vector<PendingError> DrainPendingErrors();           // uncaught errors from timers and resumed coroutines
    using RuntimeDisposing = fu2::function<void(LuaRuntime &) const>;
    void AddRuntimeDisposingListener(RuntimeDisposing listener);
};
```

**Requirements:** `HandlesFile` is true exactly for a path whose extension is `.lua` (case-sensitive); `ExecuteResourceFile` reads through `Utils::Vfs` when the path is virtual (`Utils::Vfs::IsVirtualPath`) and from disk otherwise, chunk name `@<resource>/<relative path>`; a destroyed runtime's suspended work never resumes; destroying from inside the runtime's own call is deferred to the next `Tick` (as `NodeEngine` defers); `require(name)` per spec §2 (module name restricted to `[A-Za-z0-9_.-]`, `..` refused, resolved inside the resource root only, cached per state, a module's return value or `true`), with the error `module '<name>' not found in resource '<resource>'`.

**Tests (`lua_engine`)** with temporary resource directories (pattern from `resource_manager_ut.h`): create runtime, execute a file that sets a global, read it; two resources have separate globals; error in a file returns false with `GetLastError()` naming file and line; `require("lib.util")` loads `lib/util.lua`, caches, `require("../x")` and absolute names are refused; timers survive across `Tick`; an error in a timer shows in `DrainPendingErrors` under the right resource; `DestroyResourceRuntime` stops timers and `GetRuntime` returns null; destroy from inside (a bound test function calling `DestroyResourceRuntime`) is deferred and safe; setup callback runs once per runtime; disposing listener runs before the state closes. All existing engine tests stay green.

---

### Task 11: Resources run Lua

**Files:**
- Modify: `code/framework/src/scripting/resource/resource_manager.{h,cpp}`, `resource.{h,cpp}` if needed, `code/framework/src/integrations/server/scripting/module.{h,cpp}`
- Test: `code/tests/modules/lua_resources_ut.h`

**Interfaces:**
- Consumes: Task 10, Task 7 (`RegisterValueTypesLua`).
- Produces: `void ResourceManager::SetLuaEngine(LuaEngine *engine); LuaEngine *ResourceManager::GetLuaEngine() const;` `ServerScriptingModule::GetLuaEngine() const`; the server module owns a `LuaEngine` (side Server), ticks it in `Update()` and between ticks where the Node engine runs, drains its errors into `HandleResourceRuntimeError`, and installs bindings from a `RegisterLuaBindings(LuaRuntime &)` method (value types for now).

**Requirements:**
- A script whose path ends in `.lua` runs in the resource's Lua runtime, everything else in JavaScript; execution follows manifest order across languages; `SetCurrentResourceContext` is set around each file as today.
- The Lua runtime is created when the side's execution list has a `.lua` file, and destroyed wherever the JavaScript runtime is (stop, failed start, restart, self-stop deferral). The JavaScript runtime is created only when the list has a non-`.lua` file. Without a Lua engine a `.lua` script fails the start with `Lua scripting is not available`.
- `restart`/`ensure`/the development file watcher reload a Lua resource; a failed start cleans up the state.
- An uncaught Lua error follows the resource's `errorBehavior` exactly as a JavaScript one.

**Tests (`lua_resources`)** with on-disk resources and a `NodeEngine` + `LuaEngine` + `ResourceManager`: a Lua-only resource starts, its file ran (observable through a test binding installed by the setup callback), and `NodeEngine::GetResourceRuntime(name)` is null; a JS-only resource has no Lua runtime; a mixed resource runs `a.js`, `b.lua`, `c.js` in that order (each appends to a shared native log); stop destroys the Lua runtime and its interval stops firing; restart re-reads an edited file; a syntax error fails the start with the file and line and leaves no runtime; `.lua` with no Lua engine fails with the documented message; a runtime error in a timer triggers `errorBehavior: "stop"`. Existing `resource_manager`, `resource_isolation`, `resource_lifecycle` modules stay green.

---

### Task 12: The dynamic interface — `Bind::Value`, `Bind::Call`, `Bind::Namespace`

**Files:**
- Create: `code/framework/src/scripting/bind/value.h`, `call.h`, `lua_call.cpp`, `v8_call.cpp`, `namespace.h`, `namespace.cpp`
- Modify: `bind/class.h` (context functions as `Method`/`Static` implementations; `Call::Self<T>()`)
- Test: `code/tests/modules/bind_call_ut.h`

**Interfaces:**
- Consumes: Tasks 4, 5, 8, 9.
- Produces: `Bind::Call`, `Bind::Value`, `ValueKind`, `NativeFunction = fu2::function<void(Call &)>` exactly as spec §1.4 **except** `Value::ToCallable`, `Call::Defer`, `Value::ToTransfer`, `Call::FromTransfer`, `Value::ToJson`, which Tasks 13 and 14 add (declare nothing for them yet). And:

```cpp
class Namespace final {
  public:
    Namespace(std::string name, std::string description);       // records itself in ApiCatalog (isNamespace)
    Namespace &Function(std::string name, NativeFunction function, Doc doc, Side side = Side::Shared);   // context function; Doc carries params/types explicitly
    template <typename F> Namespace &Function(std::string name, F function, Doc doc, Side side = Side::Shared);  // typed callable
    Namespace &Constant(std::string name, double value, std::string description, Side side = Side::Shared);
    Namespace &Constant(std::string name, std::string value, std::string description, Side side = Side::Shared);
    const ClassDesc &Describe() const;
    // JavaScript: sets target[name] to an object carrying the functions, and records a v8pp global_object with the docs.
    v8::Local<v8::Object> MaterialiseV8(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, v8pp::metadata::registry &catalog, Side runtime) const;
    void MaterialiseLua(lua_State *L, Side runtime) const;       // global table <name>, read-only
};
// Doc additions for context functions: Doc &Param(std::string name, TypeRef type, std::string description);
//   Doc &OptionalParam(std::string name, TypeRef type, std::string description); Doc &RestParam(std::string name, TypeRef type, std::string description);
// Standalone context function values: v8::Local<v8::Function> MakeV8Function(v8::Isolate *, v8::Local<v8::Context>, NativeFunction);
//   void PushLuaFunction(lua_State *, NativeFunction);
```

**Requirements:**
- V8 `Call`: wraps `v8::FunctionCallbackInfo`; `Value` slots index a per-call vector of `v8::Local<v8::Value>`; `ThrowTypeError`/`ThrowError` throw the V8 exception when the function returns (first throw wins, later ones ignored); `ResourceName()` is `ResourceManager::ResolveResourceContext(isolate)` when a manager is known for the isolate's engine (reach it through `CoreModules::GetScriptingModule()` or a per-isolate registration the task adds — keep it testable), else empty; `GetSide()` from `IsClientScripting(isolate)`.
- Lua `Call`: values are absolute stack slots of the running thread (`luaL_checkstack`); throws raise a Lua error after the function returns and its C++ locals are destroyed; `ResourceName()`/`GetSide()` from `LuaRuntime::From(L)`.
- `ValueKind`: JS `undefined`/`null` and Lua `nil` are `Nil`; a JS array and a Lua table with keys exactly `1..n` (n ≥ 1) are `Array`; other JS objects and Lua tables (including the empty table) are `Object`; a bound class instance is `Bound`; a function `Function`.
- `Get`/`Set`/`Keys` on objects; `Length`/`At`/`Append` on arrays (zero-based in C++); `AsInteger` is empty for a non-integral number; `AsString` of a non-string is empty (use `ToDisplayString` for `tostring`/`String()` semantics); `AsBound<T>()` honours inheritance.
- `NewFunction` returns a script function calling the native one; `Return` of nothing returns `undefined`/no value.
- A `Class` method given a `NativeFunction` receives the receiver through `call.Self<T>()` (null for a wrong or missing receiver — the function then returns without effect, the stale-receiver rule).
- Catalog: `Namespace::MaterialiseV8` records `global_object(name, description)` with each function through `v8pp::metadata` so the output matches what hand-written builtins record today (see how `scripting/builtins/chat.cpp` records `Chat`).

**Tests (`bind_call`)**: one neutral namespace `Probe` registered in both a `NodeEngine` and a `LuaRuntime`, each function exercised from both languages with equal results: `kind(v)` returning the `ValueKind` name for nil/undefined/null, booleans, numbers, strings, functions, arrays, empty and keyed objects, a bound `Vector3`; `sum(...)` variadic; `echo(v)`; `make()` returning `{ a = 1, list = {1,2,3}, nested = { b = "x" } }` built through `NewObject`/`NewArray`/`Set`/`Append`; `keys(o)`; `adder(n)` returning a `NewFunction` closure; `failType()`/`fail()` raising the right error class in JS (`TypeError`/`Error`) and a string in Lua, with code after the throw in the native function still running to its end; a `Counter` method written as a context function reading `Self<Counter>()`; `Probe.side()` and `Probe.resource()`; constants; the namespace table is read-only in Lua; the v8pp catalog has `Probe` as a `global_object` with documented functions and parameters.

---

### Task 13: Values across languages — Lua transfer and JSON

**Files:**
- Create: `code/framework/src/scripting/lua/lua_transfer.h`, `lua_transfer.cpp`, `lua_json.h`, `lua_json.cpp`
- Modify: `bind/value.h`, `call.h`, `lua_call.cpp`, `v8_call.cpp`, `bind/class.h` (host-type registration)
- Test: `code/tests/modules/lua_transfer_ut.h`, `code/tests/modules/lua_json_ut.h`

**Interfaces:**
- Consumes: Task 12, `scripting/value_transfer.h`.
- Produces:

```cpp
class LuaTransfer final {
  public:
    using HostCopy = fu2::function<bool(lua_State *, int index, std::vector<uint8_t> &bytes) const>;
    using HostRebuild = fu2::function<bool(lua_State *, const std::vector<uint8_t> &bytes) const>;   // pushes one value
    static void RegisterHostType(std::string name, HostCopy copy, HostRebuild rebuild);   // same names as ValueTransfer::RegisterHostType
    static Utils::Result<TransferredValue, std::string> Copy(lua_State *L, int index);    // functions: see Task 14 (refused until then)
    static bool Rebuild(lua_State *L, const TransferredValue &value, std::string &error); // pushes one value
};
namespace LuaJson {
    Utils::Result<std::string, std::string> Encode(lua_State *L, int index);
    bool Decode(lua_State *L, std::string_view text, std::string &error);                 // pushes one value
}
// Value::ToTransfer(), Call::FromTransfer(), Value::ToJson(), Call::ParseJson(std::string_view) -> Utils::Result<Value, std::string>
// Builtins::RegisterTransferTypes() also registers the five value types with LuaTransfer (bytes identical to the V8 side).
```

**Requirements:** the mapping of spec §4.1 in both directions, with `ValueTransfer::kMaxValues`/`kMaxBytes` bounds; refusals name the path (`value.items[2].name`); cycles refused; metatables dropped (except bound classes, which are HostObjects); JSON: objects ↔ tables, arrays ↔ sequences, `null` → `nil`, integral numbers within ±2^53 → integers, empty table → `[]`, NaN/Infinity refused, non-string keys as in §4.1; `Value::ToJson` in V8 uses `v8::JSON::Stringify`, in Lua `LuaJson::Encode`; a bound value type in JSON encodes as its `toJSON` shape in both languages (`{x,y,z}`), implemented in Lua through a `__tojson` metafield set by the class's `LuaHook`.

**Tests:** `lua_transfer`: each row of §4.1 Lua→TransferredValue→Lua, and JS→TransferredValue→Lua and Lua→TransferredValue→JS for scalars, nested tables/arrays, `Vector3`, Date, Map, Set, typed array, Error; refusals with paths; bounds. `lua_json`: encode/decode round trips, the empty-table rule, integer rule, invalid JSON error, depth limit 128, `Vector3` encodes as `{"x":..}`; the same value encoded from JS and from Lua yields the same JSON after key sorting.

---

### Task 14: `ScriptCallable` and `Deferred`

**Files:**
- Create: `code/framework/src/scripting/bind/callable.h`, `lua_callable.cpp`, `v8_callable.cpp`
- Modify: `bind/value.h`, `call.h`, `lua_call.cpp`, `v8_call.cpp`, `lua/lua_transfer.cpp`, `scripting/value_transfer.{h,cpp}`
- Test: `code/tests/modules/script_callable_ut.h`

**Interfaces:**
- Consumes: Tasks 9, 12, 13, `scripting/function_references.h`, `scripting/engine_scope.h`.
- Produces: `ScriptCallable`, `CallOutcome`, `Completion`, `ReturnMode` (spec §1.4); `std::shared_ptr<ScriptCallable> Value::ToCallable() const`; `Deferred Call::Defer()`:

```cpp
class Deferred final {            // copyable handle to shared state
  public:
    void Resolve(TransferredValue value = {});
    void Reject(std::string message);
    bool Settled() const;
};
// V8 callables additionally: class V8Callable : public ScriptCallable { v8::Isolate *Isolate() const; v8::Local<v8::Function> Function() const; };
// TransferredValue gains: std::shared_ptr<ScriptCallable> callable;   // set for a Function copied out of Lua, or by ToCallable-aware copies
```

**Requirements:**
- V8 `Invoke`: enters the function's isolate and context (the scopes `EngineScope` opens, including the callback scope), rebuilds the arguments, calls; a returned promise settles `done` later; `ReturnMode::Outcome` copies back only a boolean; an isolate being disposed (`AddRuntimeDisposingListener`) marks its callables dead and completes pending `done`s with `threw` and `runtime stopped`.
- Lua `Invoke`: rebuilds the arguments in the function's state, runs it through `LuaScheduler::Start`; `done` gets the first result when the coroutine finishes; a destroyed runtime marks callables dead the same way.
- `Call::Defer()`: in JavaScript the call returns a Promise settled by `Resolve`/`Reject`; in Lua the calling coroutine suspends when the native function returns unresolved, and resumes with the value or raises; resolving before the native function returns yields the value directly with no suspension. A deferred whose runtime is gone ignores its settlement.
- Functions cross: `LuaTransfer::Copy` of a function sets `callable`; `LuaTransfer::Rebuild` and `ValueTransfer::Rebuild` of a value with `callable` produce a function that invokes it (Lua: suspends until done; JS: returns the value when `done` ran synchronously, a Promise otherwise); a V8 function copied by `Value::ToTransfer` in the V8 backend also sets `callable`. A callable rebuilt into its own runtime is the original function.

**Tests (`script_callable`)**: JS function invoked from C++ gets arguments and returns a value; async JS function completes `done` after ticks; throwing/rejecting reports `threw`; Lua function likewise, including one that `sleep`s; `Equals` for the same function taken twice; dead after its runtime is destroyed (Invoke completes with `runtime stopped`); a `Defer`red namespace function resolved synchronously and after ticks, from JS (`await`) and from Lua (plain call inside a coroutine), and rejected; a Lua function passed to JS through `FromTransfer` and called there, and a JS function passed to Lua and called there, both sync and async; round trip to the owner yields the identical function.

---

### Task 15: Port `Console` and `ExecutionEnvironment`

**Files:** Modify `code/framework/src/scripting/builtins/console.{h,cpp}`, `execution_environment.{h,cpp}`, server module `RegisterLuaBindings`. Test: `code/tests/modules/lua_builtins_core_ut.h`.

**Requirements:** each builtin declares one definition through `Bind::Namespace`/`Bind::Class` and context functions; its existing `Register(...)` signature stays and now materialises from the definition; a `RegisterLua(LuaRuntime &runtime, ...)` is added and called from the server module's Lua setup. JavaScript behaviour, global names and the golden catalog are unchanged. In Lua, `print` and `console.log` share the output path. Tests: every function of both builtins called from Lua with the same observable effect as from JS (logger sink or command table), argument errors per the README conventions, console command handlers registered from Lua run when the command is dispatched and are dropped when the resource stops.

---

### Task 16: Port shared `Chat`

**Files:** Modify `code/framework/src/scripting/builtins/chat.{h,cpp}`. Test: `code/tests/modules/lua_builtins_chat_ut.h`.

**Requirements:** as Task 15's pattern. The option object (`{ color }`) accepts a packed integer or a `Color` in both languages; errors as today (no network peer → Error; non-player argument → TypeError). Tests mirror the existing Chat tests in `js_features_ut.h` from Lua.

---

### Task 17: Port `Entity` and `StateBag`

**Files:** Modify `code/framework/src/scripting/builtins/entity.{h,cpp}`, `state_bag.{h,cpp}`, `builtins.cpp`. Test: `code/tests/modules/lua_builtins_entity_ut.h`.

**Requirements:** `Entity` through `Bind::Class`, `entity.state` returning the `StateBag` handle in both languages; `StateBag` get/set (scalars as themselves, other values as JSON through `Value::ToJson`/`ParseJson`, 4096-byte limit as today), `onChange` subscriptions stored as `ScriptCallable` with per-resource and per-runtime cleanup (existing `CleanupResource(isolate, name)` and `UnregisterIsolate` keep working; Lua runtimes clean up through the engine's disposing listener). Handles register with `LuaTransfer` by id. Tests mirror `state_bag_scripting_ut.h` from Lua (the test world helpers there), plus a JS-set value read from Lua and a Lua subscription fired by a JS write.

---

### Task 18: Port `Player` and `TextLabel`

**Files:** Modify `code/framework/src/scripting/builtins/player.{h,cpp}`, `text_label.{h,cpp}`. Test: `code/tests/modules/lua_builtins_player_ut.h`.

**Requirements:** `Player` inherits `Entity` in both languages, server-only members tagged `Side::Server`; `TextLabel` statics (`create`, lookups) and instance members, option objects through `Bind::Value`, colour accepting packed ARGB or `Color`. Tests mirror `text_label_ut.h` and the Player tests of `js_features_ut.h` from Lua, including the side filter (`kick` absent in a client-side state).

---

### Task 19: Port `Events`

**Files:** Modify `code/framework/src/scripting/builtins/events.{h,cpp}`, `resource/resource_manager.cpp` where it emits. Test: `code/tests/modules/lua_events_ut.h`.

**Requirements:**
- Handler tables store `ScriptCallable`. The surface (`on`, `once`, `off`, `emit`, `emitTo`, `onLocal`, `emitLocal`, `listenerCount`, `onClient`, `onceClient`, `offClient`, `emitServer`, `emitAllClients`) is declared once and works identically in Lua; `on` returns an unsubscribe function in both.
- `EmitReserved`, `EmitReservedSync`, `EmitReservedTo`, `EmitClient`, `CancelPendingEmission`, `CleanupResource`, `ForgetIsolate`, `Reset`, `ClearAll`, the listener counts keep their signatures and semantics; they reach Lua handlers too (arguments copied through `ValueTransfer::Copy`).
- JavaScript-to-JavaScript dispatch inside one isolate keeps passing values by reference; across isolates it keeps going through `FunctionReferences`. Lua handlers receive copies; a Lua emit copies its arguments once.
- `emit` from Lua suspends until every handler settled and raises the aggregate error a JS caller's promise would reject with; a handler returning `false` vetoes exactly as today; the client-event table stays disjoint.
- `resourceStart`/`resourceStop` reach Lua handlers, and the lifecycle waits for a Lua handler that sleeps as it waits for a JS promise.

**Tests (`lua_events`)**: Lua→Lua, JS→Lua, Lua→JS for `on`/`emit` with scalars, tables and a `Vector3`; `once`; `off` by function and by the returned unsubscribe; `emitTo`; `onLocal`/`emitLocal` isolation between resources; `listenerCount`; veto from a Lua handler seen by `EmitReservedSync`; async Lua handler (sleeps) delays the emitter's completion; a throwing Lua handler rejects a JS emitter's promise and raises in a Lua emitter; stopping a resource drops its Lua handlers; `onClient` handlers in Lua only fire through `EmitClient`. All existing event tests in `js_features`, `resource_isolation`, `connection_gate` stay green.

---

### Task 20: Port `Exports` and `Imports`

**Files:** Modify `code/framework/src/scripting/builtins/exports.{h,cpp}`, `imports.{h,cpp}`, `resource/resource.{h,cpp}`. Test: `code/tests/modules/lua_exports_ut.h`.

**Requirements:** a resource registers an export from either language; `Resource` keeps its V8-typed export accessors for JavaScript and gains neutral storage (`TransferredValue` + callables) for values registered from Lua; importing from the other language yields a copy with functions as callables (methods keep the owner's object as `this` for JS owners, as today); manifest `exports` declarations are enforced identically; a duplicate registration fails; exports go with the runtime. Tests: Lua exports a function and a table of functions consumed by Lua and by JS (sync and async), JS exports consumed by Lua (sync, async → the Lua call suspends), errors propagate both ways, undeclared export rejected, export of a stopped resource throws/raises.

---

### Task 21: Port `Messages`

**Files:** Modify `code/framework/src/scripting/builtins/messages.{h,cpp}`. Test: `code/tests/modules/lua_messages_ut.h`.

**Requirements:** the whole surface declared once; handlers as `ScriptCallable`; request/response with timeouts works JS↔Lua in both directions; `ProcessPendingResponses(isolate, context)` keeps its signature and also settles Lua waiters (add a neutral overload the Lua engine tick calls). Tests mirror the Messages tests of `js_features_ut.h` and `resource_isolation_ut.h` from Lua and across languages.

---

### Task 22: Port server `Voice` and `PendingConnection`

**Files:** Modify `code/framework/src/integrations/server/scripting/builtins/voice.{h,cpp}`, `pending_connection.{h,cpp}`. Test: `code/tests/modules/lua_builtins_server_ut.h`.

**Requirements:** as Task 15's pattern; handles (`PendingConnection`) through `Bind::Class`; the connection-gate flow (`connection_gate_ut.h`) works with a Lua gate resource, including the deferral API. Tests mirror `connection_gate_ut.h` and the voice scripting tests from Lua.

---

### Task 23: Server wiring and cross-language integration

**Files:** Modify `code/framework/src/integrations/server/scripting/module.{h,cpp}`. Test: `code/tests/modules/lua_interop_ut.h`.

**Requirements:** `ServerScriptingModule::RegisterLuaBindings` installs every shared and server builtin in each Lua state; the module ticks the Lua engine in `Update`, runs due Lua work in `WaitUntil` (no busy loop: wake at the earliest Lua timer), drains errors, and shuts the Lua engine down after the resource manager. Tests, with real on-disk resources through `ServerScriptingModule` or the same wiring: a JS resource and a Lua resource exchanging an event with a table payload both ways; a Lua resource importing a JS resource's async export; a JS resource importing a Lua export; a mixed-language resource whose Lua file emits to its JS file; `resourceStop` handlers in Lua awaited; a Lua-only server run creates no Node resource runtime.

---

### Task 24: Client wiring and packaging

**Files:** Modify `code/framework/src/integrations/client/scripting/module.{h,cpp}`, `code/framework/src/scripting/resource/resource_packager.cpp` if it filters by extension, `code/tests/CMakeLists.txt`. Test: `code/tests/modules/lua_client_ut.h` in `FrameworkClientScriptingTests`.

**Requirements:** the client module owns a `LuaEngine` (side Client, 128 MB cap, 5 000 ms deadline), registers shared builtins in each state, ticks it in `Update`, destroys every Lua state in `Reset()`/`Shutdown()`; `.lua` files in `clientScripts`/`sharedScripts`/`files` are packaged, shipped and loaded through the VFS; `require` resolves through the VFS. Because `FrameworkClient` only builds on Windows, the engine/resource part is tested in `FrameworkClientScriptingTests` with a `V8Engine` + `LuaEngine` + `ResourceManager` (`isClient = true`): a Lua client resource starts from a mounted package or directory, shared builtins exist, the sandbox limits apply (deadline, memory), `Reset` drops the states; `module.cpp` must still pass `-fsyntax-only` with the flags of a Framework translation unit (document the command in the report).

---

### Task 25: Port client `Keybinds` and `Voice`

**Files:** Modify `code/framework/src/integrations/client/scripting/builtins/keybinds.{h,cpp}`, `voice.{h,cpp}`. Test: extend `code/tests/modules/keybinds_ut.h` usage or add `lua_client_builtins_ut.h` where the sources compile on this host.

**Requirements:** Task 15's pattern; callbacks as `ScriptCallable` with `CleanupResource` covering both languages; both files compile on a non-Windows host (verify with `-fsyntax-only` using the compile command of a Framework source from `build/compile_commands.json`, and compile them into `FrameworkClientScriptingTests` where their dependencies allow). Tests from Lua for every function whose dependencies are available in the test binary.

---

### Task 26: Port client `Chat`, `Discord`, `Nametags`

**Files:** Modify `code/framework/src/integrations/client/scripting/builtins/chat.{h,cpp}`, `discord.{h,cpp}`, `nametags.{h,cpp}`.

**Requirements:** Task 15's pattern. These need Discord and ImGui headers; verify with `-fsyntax-only` adding the in-tree include paths (`vendors/discord`), and for ImGui fetch the headers of the version vcpkg pins into `build/` for the check only. State in the report exactly what was and was not compiled.

---

### Task 27: Port client `Web`

**Files:** Modify `code/framework/src/integrations/client/scripting/builtins/web.{h,cpp}`.

**Requirements:** Task 15's pattern; event handlers and message callbacks as `ScriptCallable`; payloads through `Value::ToJson`/`ParseJson`. CEF headers exist only where CEF was downloaded; fetch the CEF headers for this host into `build/` for a `-fsyntax-only` check (reuse `cmake/DownloadCEF.cmake`'s version pin) or, if that is not possible, isolate the binding surface from the CEF calls enough that the binding half compiles and say precisely what remains unverified.

---

### Task 28: Lua definitions and the parity gate

**Files:** Create `scripts/scripting-api/build_lua_definitions.mjs` (or a C++ exporter `bind/lua_definitions.{h,cpp}` — prefer C++ so it runs in tests), `code/tests/modules/lua_parity_ut.h`. Modify server/client modules to export when `FRAMEWORK_SCRIPTING_LUA_DEFINITIONS` names a file.

**Requirements:**
- `std::string Bind::WriteLuaDefinitions(const ApiCatalog &catalog, Side side)` produces a LuaLS `---@meta` file: `---@class Name[: Base]`, `---@field` for properties, `---@param`/`---@return` for functions and methods, `---@overload` for the callable constructor, namespaces as tables. Only annotations valid for Lua 5.4.
- Parity test (`lua_parity`): register the full server set in a `NodeEngine` (as the golden test) and in a `LuaRuntime`; for every `global_object` and `constructor` symbol of the JS catalog assert the Lua global exists, and for every documented function and property assert the member exists in Lua (namespace field, class method, property getter, static). The only exemptions are an explicit list in the test (`toJSON` and anything else each port task documented as language-specific), each with its reason.
- The same test asserts the definitions file mentions every Lua global.

---

### Task 29: Documentation, example and measurements

**Files:** Create `docs/scripting_lua.md`, `docs/scripting_bindings.md`, `code/tests/bench/binding_bench.cpp` (+ CMake target `FrameworkBindingBench`), an example resource under `code/tests/fixtures/resources/lua-example/`. Modify `code/framework/src/scripting/builtins/README.md`, `AGENTS.md` (architecture and dependencies lines), `docs/scripting_resource_isolation.md` cross-reference.

**Requirements:** `scripting_lua.md` for resource authors (manifest, the Lua dialect and sandbox, API conventions, async, timers, value mapping, limits); `scripting_bindings.md` for binding authors (the `Bind` DSL, typed vs context functions, hooks, sides, how to test both languages); the bench measures `Vector3` construct/method/property and an `Events.emit` round trip in JS (before/after numbers from `git stash`-free comparison are not required — report absolute numbers) and in Lua; the example resource is started by a test in `lua_interop`.
