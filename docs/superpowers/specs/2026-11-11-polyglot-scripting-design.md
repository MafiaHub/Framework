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
`Chat`) use `Bind::Namespace` with the same member kinds minus the constructor. A constant of a namespace
whose value is only known when a runtime is set up (`ExecutionEnvironment.isClient`, `.modVersion`) is
declared with `RuntimeConstant<T>`, and each materialisation is given its value.

A member's implementation is one of:

1. **A typed callable** (member pointer, free function, captureless lambda). Parameter and return types
   come from the signature. JavaScript gets it through v8pp exactly as today; Lua gets a thunk built
   from `Bind::LuaConvert<T>`.
2. **A context function**, `void(Bind::Call &)`, for anything dynamic: variadic arguments, option
   objects, callbacks, promises. One body serves both languages (§1.4). A property may be written as two
   of them, `Property(name, type, getter, setter, description)`, when its setter takes more than one
   signature says (`entity.rotation` reads a `Quaternion` and takes a `Quaternion` or a `Vector3`): the
   getter answers with `Call::Return`, the setter reads `Call::Arg(0)`, an empty setter makes it read-only,
   and `type` is what the catalog describes it as.
3. **A per-language hook** (`V8Hook`, `LuaHook`) for what exists in one language only (`toJSON`).

Members carry a `Bind::Side` (`Shared`, `Server`, `Client`); a runtime materialises `Shared` plus its own.

A definition also says how its class leaves a runtime (§4.1):

- `Transfer(get)`: an object crosses between runtimes as itself, as the bytes of what `get` reads from it
  (`&Vector3::vec`, `&Entity::GetId` for a handle), and is built again as `T(payload)` on the other side. One
  declaration serves `ValueTransfer` and `LuaTransfer`; `RegisterTransfer()` registers both, when
  `Builtins::RegisterTransferTypes()` calls it, so the order `ValueTransfer` depends on stays in one place.
- `JsonProperties({"x", "y", "z"})`: the properties an object is written to JSON with from Lua, in that
  order. It names what `toJSON` gives in JavaScript, which stays a `V8Hook` so the catalog does not change.

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
- Instance methods are called with `:`; statics and namespace functions with `.`. A method or a property
  called on anything but an object of its class, as a method called with `.` is, raises
  `bad receiver for 'Class:method' (Class expected, got Y)`, whether it is typed or a context function.
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
    virtual Value This() const = 0;                    // the receiver as a value, to return for chaining; Nil outside a method

    virtual Value Nil() = 0;                           // undefined in JavaScript
    virtual Value Null() = 0;                          // null in JavaScript, nil in Lua; its kind is Nil
    virtual Value Boolean(bool) = 0;
    virtual Value Number(double) = 0;
    virtual Value Integer(int64_t) = 0;
    virtual Value String(std::string_view) = 0;
    virtual Value NewObject() = 0;
    virtual Value NewArray() = 0;
    virtual Value NewFunction(NativeFunction fn) = 0;  // fu2::function<void(Call &)>
    virtual Value NewFunction(NativeFunction fn, Value data) = 0;  // carries one script value the collector traces
    virtual Value Data() const = 0;                    // that value, inside such a function; Nil in any other
    virtual Utils::Result<Value, std::string> FromTransfer(const TransferredValue &) = 0;
    virtual Utils::Result<Value, std::string> ParseJson(std::string_view text) = 0;
    template <typename T> Value Wrap(T &&bound);       // a bound class instance

    virtual void Return(Value) = 0;
    virtual void ThrowTypeError(std::string message) = 0;  // raised when the function returns
    virtual void ThrowError(std::string message) = 0;
    virtual bool HasThrown() const = 0;
    virtual void WriteOutput(OutputLevel level, std::string_view line) = 0;  // the output of the runtime
    virtual size_t OutputLineLimit() const = 0;        // the bytes of a line it keeps
    virtual Deferred Defer() = 0;                      // the call's result becomes asynchronous
};

class Value {
  public:
    ValueKind Kind() const;   // Nil, Boolean, Number, String, Function, Array, Object, Bound, Other
    bool IsList() const;      // an Array, or an Object without a key: the check for "a list was passed"
    bool IsNull() const;      // JavaScript's null, which is Nil like undefined; never true in Lua
    bool AsBoolean() const;  double AsNumber() const;  std::optional<int64_t> AsInteger() const;
    std::string AsString() const;  std::string ToDisplayString() const;
    template <typename T> T *AsBound() const;
    Value Get(std::string_view key) const;  void Set(std::string_view key, Value value);
    Value Member(std::string_view key) const;                  // as a script's value.key: a bound object's property too
    std::vector<std::string> Keys() const;
    size_t Length() const;  Value At(size_t zeroBased) const;  void Append(Value value);
    std::shared_ptr<ScriptCallable> ToCallable() const;         // outlives the call
    Utils::Result<TransferredValue, std::string> ToTransfer() const;
    Utils::Result<std::string, std::string> ToJson() const;
};
```

JavaScript `undefined` and `null` are both `Nil`. `Array` is a JavaScript array or a Lua sequence
(§4.1). Indices in the C++ interface are zero-based in both languages.

- The empty Lua table is an `Object`, as nothing tells it from one, so `IsArray()` is not the check for
  a list parameter: `IsList()` is, and `Length()`/`At()` read whatever passes it.
- Only an `Object` or an `Array` has keys. A bound instance and a function have none in either language.
  `Keys()` gives integer keys ascending before string keys, and for an `Array` its indices from zero;
  `Get`/`Set` with such a key address the same element in both languages.
- A `Value` is good in its own call until that call returns. Kept beyond it, or read in a call nested in
  its own, it is `Nil`: it reads, is stored and is returned as the nil of the call it is used in.
- `ToTransfer` and `FromTransfer` are `ValueTransfer` in JavaScript and `LuaTransfer` in Lua (§4.1).
  `ToJson` is `JSON.stringify` in JavaScript, so a `toJSON` is asked, and `LuaJson::Encode` in Lua;
  `ParseJson` is `JSON.parse` and `LuaJson::Decode`. `Nil` is `null` in JSON in both languages.
- `ToTransfer`, `FromTransfer`, `ToJson` and `ParseJson` answer with a result and throw nothing, in either
  language: the function decides what a value that does not cross, or text that does not parse, means.
  One that puts up with the failure goes on, and one that does not throws the error under its own name
  (`Events.emit: Cannot copy value.job: a coroutine cannot be copied`). In JavaScript the error is the
  message of what was thrown meanwhile, by `JSON.stringify`, by a script's `toJSON` or by a rebuild, and
  that exception is not left pending. A value JavaScript has no JSON for (a function, a symbol) is an
  error, not the text `undefined`.

- `WriteOutput` is where a console writes. In JavaScript it is the scripting logger at the level given,
  `[resource] line`, or the line alone when the resource cannot be told. In Lua it is the path of `print`
  (§2). `OutputLineLimit` is the bound of a line there, and the largest `size_t` in JavaScript, which has
  none: who builds a line from many values stops once it is past the bound, so that a script cannot make
  the host spell more than a line takes.
- A JavaScript call asks the resource manager of its isolate for its resource: the one a builtin's
  `Register` was given (`Bind::SetCallResourceManager`), which is forgotten when the isolate is
  unregistered or the manager is destroyed, and otherwise the scripting module's.

- `IsNull()` is true of JavaScript's `null` and of nothing else, for an API that gives `null` a meaning a value
  left out does not have (`attachTo: null` detaches a label, `showTo: null` shows it to everyone, a key left out
  changes nothing). `IsNil()` stays true of both. It is never true in Lua, which has the one `nil` and whose
  tables cannot hold it: where such an API takes `null` inside an object, a Lua script passes `false` and the
  function accepts it there. What a function answers `null` with is `Call::Null()`, and so no key in Lua.
- `Member(key)` reads what a script reads as `value.key`, for a parameter that is "anything with an id". On an
  `Object` or an `Array` it is `Get`. On a bound object it is the property: in JavaScript whatever the object
  answers under that name, as for a script, so a class a project bound by hand and a script's subclass are read
  too; in Lua what the getter of a property its class declares for the runtime gives, a method not being one.
  What an accessor throws is thrown by the call once the function has returned, in both languages
  (`HasThrown()` tells). Anything else, a function included, has no members.

`Call::OwnerOf(function)` names the resource a function belongs to, for a builtin that keeps it beyond the
call and drops it when that resource stops. In JavaScript it is `ResourceManager::ResolveResourceContext`
with the function: the resource of the runtime, and in the one isolate of the client the resource whose
script made the function, before the one that is calling. In Lua it is the resource of the runtime.

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

`Deferred` (`Resolve(TransferredValue = {})`, `Reject(std::string)`, `Settled()`) settles the promise a
JavaScript caller received, or resumes the Lua coroutine that called. It is a copyable handle; the first
settlement counts.
`Awaited()` tells whether a settlement would still reach a script: not once it is settled, once the
runtime of its call has stopped, or when the call threw or, in Lua, could not wait. A builtin that keeps a
deferred in a table lets go of it then (`Messages::DropAbandonedRequests`).

A `ScriptCallable` need not be a script function: the host may implement one for something a script is
handed as a function and that belongs to no runtime (the `reply` of a message handler). Such a callable
says `Language::Lua`; only one that says `Language::JavaScript` may be taken for a `V8Callable`, and a
`Language::Lua` one is never downcast without asking.

- `callable.h` names neither language, so `TransferredValue` lives in `scripting/transferred_value.h`,
  which `value_transfer.h` includes.
- A function has one callable while anyone holds it, so `ToCallable()` twice gives the same one, and a
  function that stands for a callable of another runtime gives that callable.
- A callable is dead once its runtime is stopped: `IsAlive()` is false, and every `Invoke` it still owed,
  like every later one, is told `threw` and `runtime stopped`. That holds from the moment the runtime is
  stopped, in both languages, not only once it is freed.
  - A runtime stopped while script runs, as a resource that stops itself is, outlives that script until
    its engine ticks. To its callables it is gone at once, what it still owed is told then, a deferred of
    it is no longer settled, and a callable made in it during that time is dead from the start. A
    JavaScript runtime that called `process.exit()` is stopped by that call.
  - `Bind::WatchRuntimes(engine)` adds the listeners that tell: for a JavaScript engine the one that hears
    of a runtime stopped ahead of its disposal (`Engine::AddRuntimeStoppedListener`) and the disposing
    one; for the Lua engine the disposing one, as a stopped Lua runtime is told by its scheduler.
  - `Builtins::UnregisterAll` tells for an isolate no listener speaks for (the engine's own, the one
    isolate of a shared engine), and a Lua state no engine owns says so as it closes.
  - The function references between JavaScript runtimes keep their own rule: a retired runtime can be
    called through them until it is freed.
- `Invoke` calls the function on no object of the script: in JavaScript `this` is the global object of the
  function's context, as the hand-written builtins called a handler (`fn->Call(context, context->Global(), ...)`),
  and Lua gets no receiver, also for a function that crossed as a property of an object.
- `done` is told later for a promise, with one exception: a promise that settles as the call leaves its
  runtime, where the runtime's queues drain, has told `done` by the time `Invoke` returns. That is Node.
  The client's one isolate has no such queue: a promise of it, and what waits on a deferred of it, is
  told when the isolate next runs its microtasks, which is the next call into it or `V8Engine::Tick`.
- A JavaScript callable is a `Bind::V8Callable` (`bind/v8_callable.h`), which gives its isolate and its
  function to a caller that runs in the same isolate and needs no copy.
- In Lua a deferred that is settled before the function returns makes the call return or raise at once.
  Otherwise the coroutine waits; where the script cannot wait (the main thread, a coroutine it made
  itself) the call raises `<Api>.<method>: cannot wait here`. In JavaScript the call always returns a
  promise. What the function threw wins over what it deferred.

### 1.5 Error conventions

`scripting/builtins/README.md` keeps deciding which failure throws. In Lua a TypeError and an Error are
both a raised string, `"<Api>.<method>: <what went wrong>"`; a stale receiver stays a silent no-op. A
receiver that is not of the class is not stale: Lua raises for it (1.3), while JavaScript returns silently
for a wrong `this`, as the hand-written builtins did.

## 2. Runtimes

- `ScriptEngine` (`scripting/script_engine.h`) is the neutral base: language, `HandlesFile`, `Tick`,
  `CreateResourceRuntime`, `DestroyResourceRuntime`, `ExecuteResourceFile`, `GetLastError`. `Engine`
  (JavaScript) derives from it and keeps its whole public interface. `LuaEngine` derives from it.
- `ResourceManager` keeps its `Engine *` constructor and gains the Lua engine through a setter. A
  resource gets a Lua state when its script list for this side has a `.lua` file, and a JavaScript
  runtime when it has anything else, so a Lua-only resource never pays for a Node environment.
  - Which engine a script is for is asked of the engines (`HandlesFile`), the Lua one first. The scripts
    run in the order the manifest gives, whatever their language.
  - The JavaScript runtime is created first and the Lua state second, and both go wherever the JavaScript
    runtime went before: stop, a start that fails, restart, a runtime error. A `.lua` script with no Lua
    engine fails the start with `Lua scripting is not available`, before a runtime is created or a script
    has run.
  - A resource with no script at all for this side keeps the JavaScript runtime it always had.
- `LuaRuntime` is one `lua_State`: its resource, its side, its scheduler, its memory account.
  - **Libraries:** base, coroutine, table, string, utf8, math, and `os` cut down to `time`, `clock`,
    `date`, `difftime`. Removed from base: `dofile`, `loadfile`; `load` only accepts text and only
    strings; `string.dump` is removed; `collectgarbage` only answers `"count"`. `print` writes to the
    scripting logger under the resource's name, and `console` writes where `print` does
    (`LuaRuntime::WriteOutput`): a line is cut at the same bound, its control characters are blanked, it
    counts in the same share of the log per second, and it is logged at the level of its function. The
    arguments that lie past the bound are not formatted. A print handler gets the lines of both, without
    the level. `pcall`, `xpcall`, `setmetatable` and
    `coroutine.create`/`wrap`/`resume`/`close` are the runtime's own, on both sides: they behave as
    Lua's, except that `setmetatable` refuses a metatable carrying `__gc` and that none of them lets
    a script run past the deadline of an entry.
  - **`require(name)`** resolves `a.b` to `<resource>/a/b.lua`, then `<resource>/a/b/init.lua`, with the
    containment check scripts already get, through the VFS on the client. One cache per state. A name is
    letters, digits, `_`, `-` and the dots between its parts, so it names nothing outside the resource, and
    is 256 bytes long at most. The module runs once, with its name as `...`, and `require` gives what it
    returned, or `true`. It runs to its end inside the call, so it cannot wait while it loads. One that
    fails is not kept; one required again while it loads raises `require: cyclic dependency on '<name>'`;
    one that is not there raises `module '<name>' not found in resource '<resource>'`.
  - **No bytecode:** every load goes through mode `"t"`. A UTF-8 byte order mark at the start of a file
    or a module is skipped.
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
    handler, never both. With no handler set the error is logged, at the rate `print` is.
  - A script waits only in a coroutine the scheduler started. `sleep` in a coroutine the script created
    itself raises, as it does where Lua cannot yield (under `table.sort`, in `__tostring`). To a script
    the coroutine it runs in is the main thread of plain Lua: `coroutine.yield()` with no coroutine of
    its own around it ends it with `attempt to yield from outside a coroutine`, and it is not the
    script's to resume or close, from inside or from another coroutine: `coroutine.resume` answers
    `false, "cannot resume a coroutine the host started"` and `coroutine.close` raises `cannot close a
    coroutine the host started`.
  - Time is what the host passes to `Tick`. A tick fires what was due when it started, in due order, so
    a timer armed by a callback waits for the next tick. An interval is armed again from the time it was
    due; one that fell behind fires once and is next due at the first of its period boundaries after the
    tick. At most 10 000 timers per state, as in the V8 engine; one more raises. At most 64 entries
    nested in one another (a spawned function that spawns).
- `LuaEngine` owns the runtimes: it creates one with the setup callback run inside it, runs a file as the
  chunk `@<resource>/<path below the resource root>`, ticks every runtime with one reading of the clock and
  queues the errors nobody waited for, under their resource. The root of a resource is read when a file
  runs and when a module is asked for, so it may be set once the runtime exists.
  - A runtime is executing while script runs in it and while its scheduler is at work, which covers the
    `done` of a coroutine. Destroyed then, it is gone for everyone at once, fires no timer and resumes
    nothing that waits from then on, and is freed at the next tick; its name is free for a restart in
    between.
  - `Shutdown` may be called from a script. While any runtime is executing it closes none: all are taken
    away as above, and freed together once none is executing, on the way out of the engine call the
    script ran under or at the next tick.
  - Disposing listeners hear of every runtime while its state is still open. They key what they keep on
    the runtime, not on the resource name.
  - `NextDue(after)` is when a timer next fires or a sleep next ends later than `after`, for a host that
    waits between two ticks. Given the time of the last tick it passes over what that tick left due for
    the next one.
- The Lua engine holds no V8. A server running only Lua resources executes no JavaScript for them.
- The server scripting module owns the Lua engine: 512 MB a state, no deadline, and the value types,
  `console`, `ExecutionEnvironment`, `Entity`, `Player` and `TextLabel` for bindings so far. It ticks it once in every `Update()`. While it waits for its next tick the wait is
  Node's, and Lua is ticked only for a timer or a sleep that comes due after its last tick, 4 ms after
  that tick at the earliest, so Node keeps its share of the wait. A wait for no time (`sleep(0)`,
  `setTimeout(fn, 0)`) is due at the tick that armed it, not after it: it goes on at the next `Update()`,
  so a loop around `sleep(0)` goes round once for each tick of the server. What the engine queued as
  errors goes to `ResourceManager::HandleResourceRuntimeError` with Node's.
- The client scripting module owns a Lua engine too, through `LuaClientHost` (`scripting/lua/lua_client_host.h`),
  which is shared code so that it builds and is tested on every host: 128 MB a state, 5 000 ms an entry, the
  bindings both sides have (`Builtins::RegisterSharedLua`) with the members of the client side, and the transfer
  types, which nothing else registers on a client. It ticks the engine once in every `Update()` and hands what
  it queued as errors to the manager. `Reset()` and `Shutdown()` destroy every state after the resources were
  stopped; between a `Reset()` and the next `Init()` a `.lua` script fails its start with `Lua scripting is not
  available`. The builtins only a client has are added to every state through
  `LuaClientHost::AddRuntimeSetup`, which the module calls with their `RegisterLua` (`Key`, `Chat`, `Discord`,
  `Voice`, `Nametags`).

## 3. Resources

- The manifest stays `package.json` with `mafiahub.clientScripts`, `serverScripts`, `sharedScripts`,
  `files`. Lua modules reached by `require` on the client are listed in `files` (shipped, not executed).
  A resource that declares no `files` gets the scan a JavaScript one gets: every `.lua` file under the
  directories of its client and shared scripts ships, but for those named in `serverScripts`.
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

- The table is `LuaTransfer` (`scripting/lua/lua_transfer.h`). A function crosses as its callable (§4.2),
  in a state a `LuaRuntime` made.
- A Lua table is read raw: its metatable does not cross and no metamethod runs. An Object's keys are
  copied in one order, integers ascending then strings sorted by their bytes, and where a table has an
  integer key and the string key of the same text the string key is the one copied. A key that arrives as
  the text of an integer stays a string.
- A float that is a NaN, an infinity or a negative zero stays a float. The elements of an Array keep
  their indices around a hole, and the length Lua gives a table with a hole is any of its borders.
- A Map key is a string or a number, an integral one within ±2^53 an integer; a NaN or any other key is
  refused. A null or undefined property, Map value or Set member is absent.
- A bound object crosses when its class, or one of its bases, is a host type (`Bind::Class::Transfer`),
  under the name and with the bytes JavaScript uses; any other is refused, as is a host type Lua has no
  class for (`host type 'Vehicle' is not available in Lua`), as a handle a project registered for JavaScript
  alone is.
- The bounds are `ValueTransfer`'s: 128 levels, about a million values, 256 MB of strings and binary
  data (`LuaValueLimits`, which a caller may tighten). They bound what the host allocates, not only what
  it returns: a table is counted, keys and their bytes, as its keys are read, so one with more keys
  than the copy may still take is refused before they are gathered, and the keys of the tables open at
  one time count against the one bound.
- A refusal names where in the value it happened (`Cannot copy value.items[2].job: a coroutine cannot be
  copied`), counting from 1 in a value read from Lua and from 0 in one being built. A key in that path is
  cut to its first 64 bytes.
- The keys of a table are copied before its values are read, and nothing points into the state across an
  allocation. A weak table (`__mode`), which a script may make, can lose entries to the collector while
  it is walked: an entry that went is absent, or undefined where it was an element, and every entry that
  is there is under its own key.
- Neither direction lets a Lua error out, whether it is called from a script's call or from host code:
  the work is a protected call, and a state out of memory fails the copy.

### 4.2 Functions, events, exports, messages

- A function that crosses becomes a callable in the receiving runtime that runs it in its owner, with
  arguments and result copied (§4.1). A JavaScript function that returns a promise suspends a Lua caller
  until it settles; a Lua function that suspends hands a JavaScript caller a Promise. A function that
  finishes at once gives its caller the value itself, in both languages.
  - The copy carries the function as `TransferredValue::callable`: `LuaTransfer` always, `ValueTransfer`
    when it is given `Bind::V8CallableTransfer()`, as `Value::ToTransfer` and the results of a
    `ScriptCallable` are. Back in the runtime that owns it the function arrives as itself, and a
    stand-in sent on is the function it stands for, not a stand-in of a stand-in.
  - A callable has one stand-in in a runtime for as long as a script there keeps it, so a function that
    crosses twice arrives as the same function (`===`, `rawequal`). Once the stand-in is collected nothing
    is kept for it, though the callable may live on elsewhere.
  - The function references between JavaScript runtimes are as they were: a value they exported carries
    `reference` and no `callable`, and is rebuilt by their import hook. Where a value has both and the
    caller gave `ValueTransfer::Rebuild` an import hook, the hook wins; a value with a callable alone is
    rebuilt from it whatever the caller gave.
  - A function called through a callable gets the global object of its context as `this` in JavaScript and
    no receiver in Lua (§1.4), never the object it was sent with. Calling a method on that object stays what the function references of JavaScript do, and only they.
- The event, message and export tables store `ScriptCallable`. Dispatch between two JavaScript handlers
  in one isolate keeps passing values by reference, as today. Every other pairing copies.
- The JSON payload of a network event or a state bag is read and written by a C++ codec in Lua
  (`LuaJson`, `scripting/lua/lua_json.h`) with the mapping of §4.1.
  - It writes the text `JSON.stringify` writes for the same data: numbers as JavaScript spells them, a
    NaN or an infinity as `null`, strings escaped the same way, an empty table as `[]`. Object keys come
    in the order of §4.1. A bound object is the object of its class's `JsonProperties`; one without is
    refused, as are a function, a cycle, a string that is not UTF-8 and nesting beyond 128 levels.
    The bounds of §4.1 hold for it, the bytes being those of the text once escaped, and so does what
    §4.1 says of weak tables.
  - It reads `null` as `nil`, a number with an integral value within ±2^53 as an integer and any other
    as a float, and of two members with one key the last.

## 5. Tooling

- `Bind::ApiCatalog` records every definition. A generator writes LuaLS definition files (`---@meta`)
  for each side next to the TypeScript contract. Two definitions may share a global name when no runtime
  has both (the `Chat` of the server and the `Chat` of the client): `Find(name, side)` gives the one a
  runtime of that side has.
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
