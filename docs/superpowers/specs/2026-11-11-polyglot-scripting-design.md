# Polyglot Scripting (JavaScript + Lua) — Design

Date: 2026-11-11
Status: Approved for implementation (decisions 1-4 settled with the maintainer; the rest are the author's calls, marked as such)

## Goal

Add Lua beside JavaScript as a scripting language for resources, on the server and on the client, and
give the Framework its own binding layer: an API is declared once in C++ and that declaration produces
the JavaScript binding, the Lua binding and the documentation metadata. Every builtin that scripts reach
from JavaScript today is reachable from Lua under the same name.

## Non-goals and anti-goals

- **Project integration is out of scope.** kcd2, m2o, m3o and hogwarts live outside this repository and
  bind through raw V8/v8pp. Their code must keep compiling against the SDK: `Engine`, `GetIsolate()`,
  `T::GetClass(isolate)`, `Events::EmitReserved*`, `RegisterHandleTransfer<T>` and the v8pp catalog keep
  their signatures and behaviour. Porting their bindings is their work.
- **No wire change.** Script events and state bags keep travelling as scalars or JSON text. Nothing
  here is a MAJOR bump.
- No C modules, no LuaRocks, no bytecode, no `io`, no `debug` in a Lua state, on either side.
- Anti-goals: a second hand-written definition of an API; a Lua API named differently from the JS one;
  a behaviour change for existing JavaScript resources; routing same-language, same-runtime calls
  through serialisation.

## Constraints from the existing code

- `Engine` (`scripting/engine.h`) is V8-typed; `NodeEngine` gives every server resource its own isolate,
  `V8Engine` gives the client one shared isolate. `ResourceManager` owns one `Engine *`.
- Bindings are v8pp classes (`Vector3`, `Player`, `TextLabel`) or raw `FunctionCallbackInfo` callbacks
  (`Events`, `Messages`, `Voice`, `Web`, ...), all under `scripting/builtins/` and
  `integrations/{client,server}/scripting/builtins/`.
- The MafiaHub v8pp fork records a metadata catalog (`v8pp::metadata::registry`) that is exported as JSON
  (`FRAMEWORK_SCRIPTING_API_METADATA`) and consumed outside this repository to generate the TypeScript
  contract. Its content must not change for an API whose behaviour did not change.
- `ValueTransfer` / `TransferredValue` and `FunctionReferences` already copy values and call functions
  between server isolates.
- `FrameworkClient` builds on Windows only. `FrameworkTests` and `FrameworkClientScriptingTests` build
  and run on macOS and Linux and are the verification vehicle on a non-Windows host.

## Decisions

| Axis | Decision | Rationale |
|---|---|---|
| Binding strategy | Own C++ builder, `Framework::Scripting::Bind`, recording a neutral description and per-language thunks | No extra build step, types come from the real signatures, extends the catalog that exists. Rejected: a second hand-written Lua binding set (drift), an external IDL with code generation (a toolchain in every build, a poor fit for classes and callbacks), ScriptX (bound classes must inherit its base, backend fixed at build time). |
| Lua runtime | PUC Lua **5.5.1**, vcpkg port `lua` with feature `cpp` (`unofficial::lua::lua-cpp`) | The maintainer wants a current Lua. 5.5 is the current release, has native 64-bit integers, builds on every target including 32-bit Windows, and is in the pinned vcpkg baseline. Rejected: LuaJIT and Luau (both Lua 5.1 dialects). *(author's call within "recent Lua")* |
| Lua linkage | Compiled as C++, so a Lua error is a C++ exception | An error unwinding through a binding runs destructors. Include `lua.h`, `lauxlib.h`, `lualib.h` directly, never `lua.hpp` (it declares C linkage). |
| JS backend | Materialises through v8pp | Projects inherit framework classes with `inherit<Entity>()` and read `T::GetClass(isolate)`; the v8pp catalog stays the JS export. |
| Lua state | One per resource, on both sides | Isolation and hot restart for free; a state is small. |
| Mixed languages | Per file, by extension: `.lua` runs in the resource's Lua state, everything else in JavaScript as today. Scripts run in manifest order. | As FiveM. The two languages of one resource share nothing but events and exports. |
| API naming | Identical names in both languages | One documentation set. Lua idioms (operators, `:` calls) are added, never substituted. |
| Cross-runtime values | `TransferredValue` | Already the neutral form; it gains a Lua copy and rebuild. |
| Async | A function documented as returning a Promise suspends the calling Lua coroutine and returns the settled value, or raises | No `await` keyword to teach; every Lua entry point runs in a coroutine. |
| Catalog | The v8pp registry stays the JavaScript export, filled from the neutral description; a neutral catalog is added for Lua definitions | Zero change for the existing TypeScript tooling. |

## 1. The binding layer (`code/framework/src/scripting/bind/`)

### 1.1 Declaring an API

```cpp
static const Bind::Class<Vector3> &Definition() {
    static const auto def = Bind::Class<Vector3>("Vector3", "Mutable three-dimensional vector ...")
        .Constructor<float, float, float>(Bind::Doc("Creates a vector from three numeric components.")
            .Param("x", "Initial X component.").Param("y", "...").Param("z", "..."))
        .Method("add", &Vector3::add, Bind::Doc("Adds another vector to this vector in place.")
            .Param("other", "Vector to add component-wise.").Returns("This mutated vector for chaining."))
        .Property("x", &Vector3::getX, &Vector3::setX, "Mutable X component.")
        .Static("zero", &Vector3::zero, Bind::Doc("Creates a zero vector.").Returns("New Vector3(0, 0, 0)."))
        .LuaMeta(Bind::LuaMetaEvent::Add, +[](const Vector3 &a, const Vector3 &b) { return Vector3(a.vec() + b.vec()); });
    return def;
}
```

A definition is built once per process and materialised into each runtime. Service objects (`Events`,
`Chat`) use `Bind::Namespace` with the same member kinds minus the constructor.

A member's implementation is one of:

1. **A typed callable** (member pointer, free function, captureless lambda). Parameter and return types
   come from the signature. JavaScript gets it through v8pp exactly as today; Lua gets a thunk built
   from `Bind::LuaConvert<T>`.
2. **A context function**, `void(Bind::Call &)`, for anything dynamic: variadic arguments, option
   objects, callbacks, promises. One body serves both languages (§1.4).
3. **A per-language hook** (`V8Hook`, `LuaHook`) for what exists in one language only (`toJSON`).

Members carry a `Bind::Side` (`Shared`, `Server`, `Client`); a runtime materialises `Shared` plus its own.

### 1.2 Type model

| C++ | Neutral | TypeScript | Lua annotation | Lua runtime value |
|---|---|---|---|---|
| `bool` | Boolean | `boolean` | `boolean` | boolean only |
| `float`, `double` | Number | `number` | `number` | integer or float accepted, float pushed |
| integral types | Integer | `number` | `integer` | integer, or a float with an integral value; `uint64_t` crosses as its two's-complement `lua_Integer` |
| `std::string`, `std::string_view`, `const char *` | String | `string` | `string` | string only, no number coercion |
| `std::optional<T>` | Optional(T) | optional parameter / `T \| undefined` | `T?` | `nil` or absent |
| `std::vector<T>` | Array(T) | `T[]` | `T[]` | sequence, 1-based |
| bound class `T`, `T &`, `const T &`, `T *` | Class(name) | name | name | full userdata |
| `glm::vec2/3/4`, `glm::quat` | Class(`Vector2`...`Quaternion`) | as the class | as the class | the class's userdata |
| a method on `T` returning `T &` | This | `this` | `self` | the receiver itself, not a copy |
| `void` | Void | `void` | `nil` | no value |

`Bind::Doc` supplies parameter names and descriptions in order, and can override a derived spelling
(`Bind::Type::Custom(ts, lua)`) or mark a parameter optional for documentation.

### 1.3 Lua materialisation

- An instance is a full userdata holding the object by value, with one metatable per class per state,
  found through the registry by the definition's address. `__gc` exists only for a type with a
  non-trivial destructor. `__metatable` is `false`.
- `__index` resolves methods, then property getters; an unknown key reads `nil`. `__newindex` resolves
  property setters; an unknown or read-only key raises. A method named `toString` also becomes
  `__tostring`.
- The class is a table published under its name: statics as fields, `new` as the constructor, and
  callable (`Vector3(1, 2, 3)`). The table is read-only.
- Inheritance: a derived class's metatable chains to its base's methods and properties; a parameter of a
  base type accepts a derived instance (the definition stores the upcast).
- Instance methods are called with `:`; statics and namespace functions with `.`.
- A wrong argument type or count raises `bad argument #n to 'Class:method' (X expected, got Y)` /
  `wrong number of arguments to 'Class:method'`. A C++ exception from the implementation becomes a Lua
  error carrying `what()`. A thunk catches `std::exception` only, never Lua's own unwinding, and raises
  after the catch block.

### 1.4 The dynamic interface

`Bind::Call` is what a context function receives; `Bind::Value` is a handle to a script value that lives
until the call returns. Both are abstract over the language; no builtin written against them names V8 or
Lua.

```cpp
class Call {
  public:
    virtual Language GetLanguage() const = 0;
    virtual Side GetSide() const = 0;
    virtual std::string ResourceName() const = 0;      // the resource the calling script belongs to
    virtual ResourceManager *GetResourceManager() const = 0;
    virtual size_t ArgCount() const = 0;
    virtual Value Arg(size_t index) const = 0;         // Nil beyond ArgCount
    template <typename T> T *Self() const;             // receiver of a method call, or nullptr

    virtual Value Nil() = 0;
    virtual Value Boolean(bool) = 0;
    virtual Value Number(double) = 0;
    virtual Value Integer(int64_t) = 0;
    virtual Value String(std::string_view) = 0;
    virtual Value NewObject() = 0;
    virtual Value NewArray() = 0;
    virtual Value NewFunction(NativeFunction fn) = 0;  // fu2::function<void(Call &)>
    virtual Value FromTransfer(const TransferredValue &) = 0;
    template <typename T> Value Wrap(T &&bound);       // a bound class instance

    virtual void Return(Value) = 0;
    virtual void ThrowTypeError(std::string message) = 0;  // raised when the function returns
    virtual void ThrowError(std::string message) = 0;
    virtual bool HasThrown() const = 0;
    virtual Deferred Defer() = 0;                      // the call's result becomes asynchronous
};

class Value {
  public:
    ValueKind Kind() const;   // Nil, Boolean, Number, String, Function, Array, Object, Bound, Other
    bool AsBoolean() const;  double AsNumber() const;  std::optional<int64_t> AsInteger() const;
    std::string AsString() const;  std::string ToDisplayString() const;
    template <typename T> T *AsBound() const;
    Value Get(std::string_view key) const;  void Set(std::string_view key, Value value);
    std::vector<std::string> Keys() const;
    size_t Length() const;  Value At(size_t zeroBased) const;  void Append(Value value);
    std::shared_ptr<ScriptCallable> ToCallable() const;         // outlives the call
    Utils::Result<TransferredValue, std::string> ToTransfer() const;
    Utils::Result<std::string, std::string> ToJson() const;
};
```

JavaScript `undefined` and `null` are both `Nil`. `Array` is a JavaScript array or a Lua sequence
(§4.1). Indices in the C++ interface are zero-based in both languages.

`ScriptCallable` is a script function kept beyond a call: an event handler, a key bind, an export.

```cpp
struct CallOutcome { bool threw = false; std::string error; TransferredValue value; };
using Completion = fu2::unique_function<void(CallOutcome &&)>;
enum class ReturnMode : uint8_t { Value, Outcome };   // Outcome: a boolean or nothing, as FunctionReferences::Returned

class ScriptCallable {
  public:
    virtual ~ScriptCallable() = default;
    virtual Language GetLanguage() const = 0;
    virtual std::string ResourceName() const = 0;
    virtual bool IsAlive() const = 0;                  // false once its runtime is gone
    virtual bool Equals(const ScriptCallable &other) const = 0;
    // Runs the function with copies of `args`. `done` runs exactly once, on the scripting thread: before
    // Invoke returns when the function finished synchronously, later when it returned a promise (JS) or
    // suspended (Lua).
    virtual void Invoke(const std::vector<TransferredValue> &args, ReturnMode mode, Completion done) = 0;
};
```

`Deferred` (`Resolve(TransferredValue)`, `Reject(std::string)`) settles the promise a JavaScript caller
received, or resumes the Lua coroutine that called.

### 1.5 Error conventions

`scripting/builtins/README.md` keeps deciding which failure throws. In Lua a TypeError and an Error are
both a raised string, `"<Api>.<method>: <what went wrong>"`; a stale receiver stays a silent no-op.

## 2. Runtimes

- `ScriptEngine` (`scripting/script_engine.h`) is the neutral base: language, `HandlesFile`, `Tick`,
  `CreateResourceRuntime`, `DestroyResourceRuntime`, `ExecuteResourceFile`, `GetLastError`. `Engine`
  (JavaScript) derives from it and keeps its whole public interface. `LuaEngine` derives from it.
- `ResourceManager` keeps its `Engine *` constructor and gains the Lua engine through a setter. A
  resource gets a Lua state when its script list for this side has a `.lua` file, and a JavaScript
  runtime when it has anything else, so a Lua-only resource never pays for a Node environment.
- `LuaRuntime` is one `lua_State`: its resource, its side, its scheduler, its memory account.
  - **Libraries:** base, coroutine, table, string, utf8, math, and `os` cut down to `time`, `clock`,
    `date`, `difftime`. Removed from base: `dofile`, `loadfile`; `load` only accepts text and only
    strings; `string.dump` is removed; `collectgarbage` only answers `"count"`. `print` writes to the
    scripting logger under the resource's name. `pcall`, `xpcall`, `setmetatable` and
    `coroutine.create`/`wrap`/`resume`/`close` are the runtime's own, on both sides: they behave as
    Lua's, except that `setmetatable` refuses a metatable carrying `__gc` and that none of them lets
    a script run past the deadline of an entry.
  - **`require(name)`** resolves `a.b` to `<resource>/a/b.lua`, then `<resource>/a/b/init.lua`, with the
    containment check scripts already get, through the VFS on the client. One cache per state. A name is
    letters, digits, `_`, `-` and the dots between its parts, so it names nothing outside the resource. The
    module runs once, with its name as `...`, and `require` gives what it returned, or `true`. It runs to
    its end inside the call, so it cannot wait while it loads. One that fails is not kept; one required
    again while it loads raises `require: cyclic dependency on '<name>'`; one that is not there raises
    `module '<name>' not found in resource '<resource>'`.
  - **No bytecode:** every load goes through mode `"t"`.
  - **Memory:** a `lua_Alloc` that refuses past the cap. Defaults: 128 MB on the client, 512 MB on the
    server, `0` for no cap.
  - **Time:** a count hook every 10 000 instructions checks a deadline per entry into the state. Defaults:
    5 000 ms on the client, off on the server. Past it the entry is aborted with `script ran too long`.
  - **Errors:** every entry is a protected call with a traceback handler; an error in a handler or timer
    is attributed to the resource and handed to `ResourceManager::HandleResourceRuntimeError`.
- **Scheduler** (C++): every script file, handler and timer callback runs in its own coroutine. Globals:
  `setTimeout`, `setInterval`, `clearTimeout`, `clearInterval` (as JavaScript), plus Lua-only `sleep(ms)`
  and `spawn(fn, ...)`. Timers die with the state.
  - Each stretch a coroutine runs, from its start or a resume to its end or its next wait, is one entry
    with its own deadline. A coroutine that fails tells whoever waits on it, or else the resource's error
    handler, never both.
  - A script waits only in a coroutine the scheduler started. `sleep` in a coroutine the script created
    itself raises, as it does where Lua cannot yield (under `table.sort`, in `__tostring`). To a script
    the coroutine it runs in is the main thread of plain Lua: `coroutine.yield()` with no coroutine of
    its own around it ends it with `attempt to yield from outside a coroutine`.
  - Time is what the host passes to `Tick`. A tick fires what was due when it started, in due order, so
    a timer armed by a callback waits for the next tick. An interval is armed again from the time it was
    due, and fires once per tick until it has caught up. At most 10 000 timers per state, as in the V8
    engine; one more raises. At most 64 entries nested in one another (a spawned function that spawns).
- `LuaEngine` owns the runtimes: it creates one with the setup callback run inside it, runs a file as the
  chunk `@<resource>/<path below the resource root>`, ticks every runtime with one reading of the clock and
  queues the errors nobody waited for, under their resource. A runtime destroyed from inside its own call
  is gone for everyone at once, fires no timer and resumes nothing that waits from then on, and is freed
  at the next tick; its name is free for a restart in between. Disposing listeners hear of every runtime
  while its state is still open.
- The Lua engine holds no V8. A server running only Lua resources executes no JavaScript for them.

## 3. Resources

- The manifest stays `package.json` with `mafiahub.clientScripts`, `serverScripts`, `sharedScripts`,
  `files`. Lua modules reached by `require` on the client are listed in `files` (shipped, not executed).
- Stop, restart, `ensure` and the development file watcher treat a Lua state as they treat a JavaScript
  runtime: handlers, timers, exports and subscriptions go with it.
- Exports declared in the manifest may be registered from either language; a second registration of the
  same name fails.

## 4. Crossing languages

### 4.1 Values

| `TransferredValue` | From Lua | Into Lua |
|---|---|---|
| Undefined, Null | `nil` produces Undefined | `nil` |
| Boolean | boolean | boolean |
| Number | float; integer within ±2^53 | an integral value within ±2^53 arrives as an integer, anything else as a float |
| BigInt | integer beyond ±2^53 | integer when it fits 64 bits, refused otherwise |
| String | valid UTF-8 string; other strings are refused | string |
| Array | table whose keys are exactly `1..n`; **an empty table is an empty Array** | sequence; a `null` element is a hole |
| Object | any other table with string or integer keys (integer keys become their decimal text); other key types are refused | table with string keys |
| Date | — | integer milliseconds since the epoch |
| Map, Set | — | table (string/number keys only) / sequence |
| ArrayBuffer, TypedArray | — | string of the bytes |
| Error | — | table `{ name, message, stack }` |
| HostObject | bound value type or handle | the same class |
| Function | function | callable |
| refused | cycles, coroutines, unbound userdata, metatables are dropped | RegExp |

One value crosses per call result: a Lua function's first return value.

### 4.2 Functions, events, exports, messages

- A function that crosses becomes a callable in the receiving runtime that runs it in its owner, with
  arguments and result copied (§4.1). A JavaScript function that returns a promise suspends a Lua caller
  until it settles; a Lua function that suspends hands a JavaScript caller a Promise.
- The event, message and export tables store `ScriptCallable`. Dispatch between two JavaScript handlers
  in one isolate keeps passing values by reference, as today. Every other pairing copies.
- The JSON payload of a network event or a state bag is read and written by a C++ codec in Lua with the
  mapping of §4.1.

## 5. Tooling

- `Bind::ApiCatalog` records every definition. A generator writes LuaLS definition files (`---@meta`)
  for each side next to the TypeScript contract.
- A parity test walks the JavaScript catalog and requires every global and member to exist in a Lua
  state of the same side, apart from a short explicit list of language-specific members.

## 6. Testing

- Unit tests per layer in `code/tests/modules/*_ut.h`, driven by `FrameworkTests` (server and shared) and
  `FrameworkClientScriptingTests` (client runtime).
- A golden test pins the v8pp catalog of every builtin before it is ported and stays green after.
- A parity suite runs the same expression in JavaScript and in Lua against each ported builtin.
- The existing JavaScript suites (`js_features`, `resource_isolation`, `state_bag_scripting`,
  `function_references`, `value_transfer`, ...) are the regression net and must stay green.
- Client builtins whose dependencies exist only on Windows (CEF, ImGui) cannot be linked on another
  host. Their ports are verified by a syntax-check target where headers resolve, and by review.

## 7. Version impact and licence

Scripting-layer change: MINOR. Lua (MIT) is added to the third-party list in `LICENSE.txt` and
`NOTICE.txt`. The work is an upstream contribution under clause 5.

## 8. Risks

- PUC Lua's sandbox is assembled, not built in. Text-only loading, no `debug`, the memory cap and the
  deadline are the defence. The hook does not fire inside a C function, so any library loop written
  in C can still stall a client: pattern matching on a pathological pattern, `table.move` over a huge
  range. A pathological regular expression can do the same today.
- A script can read addresses of its own process: `string.format("%p", v)` and `tostring` of a
  table, a function or a userdata print them.
- Script-defined `__gc` finalizers are not available: Lua runs a finalizer with hooks off, where the
  deadline cannot reach it.
- The count hook costs about half the speed of the Lua VM in a state that has a deadline, which by
  default is every client state.
- A `uint64_t` id above 2^63 reads as a negative integer in Lua.
- LuaLS support for the 5.5 dialect is unverified; definitions use only annotations valid for 5.4.
- A 32-bit Windows build of the Lua port and of the Windows-only client builtins cannot be exercised on
  a macOS host.
