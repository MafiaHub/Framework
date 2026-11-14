# Scripting bindings

How to expose a C++ API to scripts. This is for whoever writes a binding, in
the Framework or in a project built on it. What a script author sees of Lua is
in [scripting_lua.md](scripting_lua.md); which failure throws, how a builtin
registers and how it is named is in
[`scripting/builtins/README.md`](../code/framework/src/scripting/builtins/README.md),
which stays normative.

Resources are written in JavaScript or in Lua, per file. An API is declared
once, with `Framework::Scripting::Bind` (`code/framework/src/scripting/bind/`),
and that one definition gives:

- the JavaScript binding, built through v8pp as a binding written by hand is;
- the Lua binding, under the same names;
- the v8pp catalog the TypeScript declarations are generated from;
- the description the Lua definition files are generated from.

A definition is built once per process, is never destroyed, and is
materialised into each runtime that is set up.

## Declaring a class

A bound C++ class is named for scripts with `FW_SCRIPT_CLASS`, at global
namespace scope after the class is defined:

```cpp
FW_SCRIPT_CLASS(Framework::Scripting::Builtins::Vector3, "Vector3");
```

Its definition is a `Bind::Class<T>` (`bind/class.h`). This is `Vector3`
(`builtins/vector3.cpp`), with the repetitive members left out:

```cpp
const Bind::Class<Vector3> &Vector3::Definition() {
    // AddVectorOperators gives the class its Lua operators.
    static const auto definition = AddVectorOperators(Bind::Class<Vector3>("Vector3", "Mutable three-dimensional vector for positions, directions, and Euler angles.")
            .Constructor<float, float, float>(Framework::Scripting::Bind::Doc("Creates a vector from three numeric components.").Param("x", "Initial X component.").Param("y", "Initial Y component.").Param("z", "Initial Z component."))
            .Method("add", &Vector3::add, Framework::Scripting::Bind::Doc("Adds another vector to this vector in place.").Param("other", "Vector to add component-wise.").Returns("This mutated vector for chaining."))
            // ...
            .Method("toString", &Vector3::toString, Framework::Scripting::Bind::Doc("Formats this vector for logging and debugging.").Returns("Text in Vector3(x, y, z) form."))
            .Property("x", &Vector3::getX, &Vector3::setX, "Mutable X component.")
            // ...
            .Property("length", &Vector3::getLength, "Read-only Euclidean magnitude of this vector.")
            .Static("zero", &Vector3::zero, Framework::Scripting::Bind::Doc("Creates a zero vector.").Returns("New Vector3(0, 0, 0)."))
            // ...
            // What JSON.stringify asks a JavaScript object for. Lua writes the same object from JsonProperties.
            .V8Hook([](v8pp::class_<Vector3> &cls, v8::Isolate *) {
                cls.prototype_function("toJSON", &ToJson, v8pp::metadata::docs("{ x: number; y: number; z: number }", {}, "Converts this vector to a plain object for JSON.stringify.", "Object containing the current components."));
            })
            .JsonProperties({"x", "y", "z"})
            // Crosses between runtimes as its glm value.
            .Transfer(&Vector3::vec));
    return definition;
}

v8pp::class_<Vector3> &Vector3::GetClass(v8::Isolate *isolate) {
    return _classes.Get(isolate, [isolate]() {
        return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
    });
}

void Vector3::Register(Bind::Registrar &registrar, Bind::Languages languages) {
    registrar.Global(Definition(), languages);
}
```

The builder:

| Call | What it declares |
|---|---|
| `Constructor<Args...>(doc)` | `new Name(...)` and `Name(...)` in JavaScript, `Name.new(...)` and `Name(...)` in Lua. One per class. Without it no script can construct an object. |
| `Method(name, f, doc, side)` | An instance method: `obj.name()` in JavaScript, `obj:name()` in Lua. |
| `Static(name, f, doc, side)` | A function of the class: `Name.f()` in both. |
| `Property(name, getter, description, side)` | A read-only property. |
| `Property(name, getter, setter, description, side)` | A property a script assigns. |
| `Property(name, type, getter, setter, description, side)` | A property written as two context functions; an empty setter makes it read-only. |
| `Inherit<Base>()` | The methods, properties and operators of `Base`; an instance passes where a `Base` is expected. `Base` is defined first. Statics, the constructor and hooks are not inherited. |
| `ValueOnly()` | No global: scripts only ever get the objects (`StateBag`, `PendingConnection`). |
| `JsonProperties`, `Transfer` | How an object leaves a runtime, see [Crossing runtimes](#crossing-runtimes). |
| `V8Hook`, `V8Constructor`, `V8EnsureBase`, `LuaHook`, `LuaMeta` | What one language only has, see [Per-language hooks](#per-language-hooks). |

A member cannot be overloaded: a second method or property of one name throws
`std::logic_error` when the definition is built, as does a second definition of
one C++ type, and a name that is not the one `FW_SCRIPT_CLASS` gave. A class
may declare again what a base declares. A method named `toString` is also what
Lua's `tostring()` calls.

`Bind::Doc` (`bind/doc.h`) gives what a signature cannot: the summary,
`Param(name, description)` for each parameter in order, `Returns(description)`.
`OptionalParam` documents a parameter as optional whatever its C++ type (see
the warning below), `ParamType` and `ReturnType` replace a derived type, and `RestParam` is the
last parameter of a context function, which takes every remaining argument. A
type the model cannot derive is spelled by hand for both languages:
`Bind::Type::Custom("{ author?: string }", "table")`. A `Doc` that disagrees
with the signature on the number of parameters throws, naming
`<Class>.<member>`.

> **Warning.** On a typed member, `OptionalParam` only documents the
> parameter as optional. It does not make the argument omittable in either
> language: only a `std::optional<T>` parameter does. This is why
> `Player.kick` requires the reason its documentation calls optional
> (`wrong number of arguments to 'Player:kick'` in Lua,
> `Argument count does not match function definition` in JavaScript).

### The type model

A typed member gets its parameter and return types from its C++ signature
(`bind/types.h`, `Bind::TypeOf<T>()`). A type outside this table does not
compile.

| C++ | TypeScript | Lua annotation | In Lua |
|---|---|---|---|
| `bool` | `boolean` | `boolean` | a boolean only |
| `float`, `double` | `number` | `number` | an integer or a float |
| integral types, enums | `number` | `integer` | an integer, or a float with an integral value |
| `std::string`, `std::string_view`, `const char *` | `string` | `string` | a string only |
| `std::optional<T>` | optional | `T?` | `nil` or absent; a trailing one may be omitted in both languages |
| `std::vector<T>` | `T[]` | `T[]` | a sequence |
| a bound class, by value, reference or pointer | its name | its name | its userdata |
| `glm::vec2/3/4`, `glm::quat` | `Vector2`...`Quaternion` | the same | the userdata of the value type |
| a method of `T` returning `T &` | `this` | `self` | the receiver itself |
| `void` | `void` | `nil` | no value |

## Declaring a namespace

A global object of functions and constants, such as a service a script reaches
as `Chat`, is a `Bind::Namespace` (`bind/namespace.h`): what a class is,
without instances. This is the server's `Chat` (`builtins/chat.cpp`):

```cpp
void SendToAll(Bind::Call &call) {
    if (!call.Arg(0).IsString()) {
        call.ThrowTypeError("Chat.sendToAll: expected (text, opts?)");
        return;
    }
    std::string author;
    uint32_t color = 0;
    ReadOptions(call.Arg(1), author, color);
    if (!Send(call.Arg(0).AsString(), author, color, MafiaNet::UNASSIGNED_RAKNET_GUID, true)) {
        call.ThrowError("Chat.sendToAll: network peer unavailable");
    }
}

const Bind::Namespace &Chat::Definition() {
    static const auto definition = [] {
        const auto options      = Bind::Type::Custom("{ author?: string; color?: number | Color }", "{ author: string?, color: (integer|Color)? }");
        const char *optionsHelp = "Optional author label and color: a Color or a packed 0xRRGGBBAA integer.";
        return Bind::Namespace("Chat", "Server-side delivery of structured chat messages to connected clients.", Framework::Scripting::Bind::Side::Server)
            .Function("sendToAll", &SendToAll, Framework::Scripting::Bind::Doc("Broadcasts a structured chat message to all clients.").Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Message body broadcast to every connected client.").OptionalParam("options", options, optionsHelp), Framework::Scripting::Bind::Side::Server)
            .Function("sendToPlayer", &SendToPlayer,
                Framework::Scripting::Bind::Doc("Sends a structured chat message to one player's owning connection.")
                    .Param("player", Framework::Scripting::Bind::TypeOf<Entity>(), "Player-owned entity identifying the destination connection.")
                    .Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Message body sent to the player.")
                    .OptionalParam("options", options, optionsHelp),
                Framework::Scripting::Bind::Side::Server);
    }();
    return definition;
}

void Chat::Register(Bind::Registrar &registrar, Bind::Languages languages) {
    registrar.Global(Definition(), languages);
}
```

- `Function(name, f, doc, side)` takes a context function, or a typed
  callable (a function, a static member function, a captureless lambda).
- `Constant(name, value, description, side)` is a boolean, a number or a string
  a script reads and cannot replace.
- `RuntimeConstant<T>(name, description, side)` declares a constant whose value
  is only known when a runtime is set up (`ExecutionEnvironment.isClient`,
  `.modVersion`). `MaterialiseV8` and `RegisterLuaGlobal` are given the values
  as `Namespace::RuntimeConstants`, and throw when one is missing.
- In Lua the namespace is a table a script cannot write to. In JavaScript it is
  a plain object, as a builtin written by hand makes.

`MaterialiseV8` sets the global and records the catalog. `RegisterLuaGlobal`
runs unprotected, for a class as for a namespace: call it from the setup
callback of the Lua engine, which protects it.

## Registration

A builtin has one `Register`, for both languages:

```cpp
static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All /*, ResourceManager *, ... */);
```

`Bind::Registrar` (`bind/registrar.h`) is what it registers through, so a
builtin includes no Lua header:

| Call | What it does |
|---|---|
| `Global(namespaceOrClass, languages[, constants])` | Publishes the definition as a global. For a namespace it returns the JavaScript object, empty when none was made. `constants` is a `Namespace::RuntimeConstants`, or a function of the `Bind::Side` that gives them. |
| `Type(class, languages)` | Makes a class known without a global, for a value that reads as it (`StateBag`, `PendingConnection`). Only Lua has anything to do. |
| `HasJavaScript(languages)`, `Isolate()`, `Context()`, `Target()` | For what a builtin keeps for its isolate: handler tables, `Bind::SetCallResourceManager`, catalog metadata. |
| `Lua()` | The `Bind::LuaBindings` recorded: `Apply(runtime)` publishes them in a Lua state. |

`Bind::Languages` is `JavaScript`, `Lua` or `All`, combined with `|` and `&`.
A registrar built for an isolate
(`Registrar(isolate, context, target, side)`) registers JavaScript at once;
one built without (`Registrar()`) registers Lua only. Lua is always
recorded, because a Lua state is made per resource, after the builtins
register: the scripting modules keep the record of the registrar of their
isolate and apply it to each state as it is created, with the side of that
runtime, before the setups of a project.


A member is implemented one of two ways.

**A typed callable** where the C++ signature says everything. A method, a
getter or a setter takes a member function of `T` or of one of its bases, or a
function or captureless lambda that takes the receiver first, as `T &`,
`const T &` or `T *`. A static takes a function, a static member function or a
lambda. JavaScript gets it through v8pp, which converts the arguments and words
the errors; Lua through `Bind::LuaConvert<T>`, which raises
`bad argument #1 to 'Class:method' (integer expected, got string)`.

**A context function**, `void(Bind::Call &)` (`Bind::NativeFunction`), for
what a signature cannot say: variadic arguments, option objects, callbacks,
results that come later, an argument of one of several types. One body is
called from both languages and names neither. Its `Doc` gives every parameter
with its type, and the return type with `ReturnType` (void without it). A
`std::exception` it throws reaches the script as an Error carrying `what()`.

The two behave differently in a few documented places
([below](#differences-between-javascript-and-lua)); a context function reads
its arguments itself, so it behaves the same in both.

### `Bind::Call`

What a context function receives (`bind/call.h`):

- **Who calls.** `GetLanguage()`, `GetSide()`, `ResourceName()` (empty when it
  cannot be told), `GetResourceManager()`, `OwnerOf(function)`.
- **Arguments.** `ArgCount()` and `Arg(index)`, which is Nil beyond the count.
  The receiver of a method is not an argument, in either language.
- **The receiver.** `Self<T>()` is the object a method was called on, as a `T`
  or the `T` part of a derived object; `This()` is the receiver as a value, to
  return for chaining.
- **Making values.** `Nil()` (`undefined` in JavaScript), `Null()` (`null` in
  JavaScript, `nil` in Lua), `Boolean`, `Number`, `Integer`, `String`,
  `NewObject()`, `NewArray()`, `NewFunction(fn)`, `NewFunction(fn, data)`,
  `Wrap(bound)` for a new instance of a bound class, `FromTransfer(value)`,
  `ParseJson(text)`.
- **Answering.** `Return(value)`, with overloads for `bool`, `double`, integers
  and strings. Without it the function returns nothing. Called again, it
  replaces the result.
- **Throwing.** `ThrowTypeError`, `ThrowRangeError`, `ThrowError`,
  `HasThrown()`.
- **Output.** `WriteOutput(level, line)` writes where the console of the
  runtime writes; `OutputLineLimit()` is the bytes of a line it keeps.
- **Later.** `Defer()`, see [Asynchronous results](#asynchronous-results).

Throwing does not leave the function. It runs on to its return, and the script
gets the error then: a `TypeError`, `RangeError` or `Error` in JavaScript, a
raised string in Lua. The first throw wins and what the function returned is
dropped, so return after throwing:

```cpp
if (!call.Arg(1).IsString()) {
    call.ThrowTypeError("Chat.sendToPlayer: expected (player, text, opts?)");
    return;
}
```

Which failure is a TypeError, which an Error and which a silent no-op is the
README's to say. The message is `"<Api>.<method>: <what went wrong>"`, and
nothing is added to it.

A stale receiver returns silently:

```cpp
auto *self = call.Self<Entity>();
if (!self) {
    return;
}
```

In JavaScript `Self` is null for a wrong `this`. In Lua a method or a property
called on anything but an object of its class never reaches the function: the
layer raises `bad receiver for 'Class:method' (...)`.

A JavaScript call asks the resource manager of its isolate for its resource.
A builtin whose `Register` is given a `ResourceManager *` says which one that
is with `Bind::SetCallResourceManager(isolate, manager)`, as `console.cpp`
does; a Lua runtime is told by the scripting module that sets it up.

### `Bind::Value`

A handle to a script value (`bind/value.h`): an argument, or what the `Call`
or another `Value` made.

**A handle is valid only while its call is the innermost one running.** Once
the call has returned, and inside a call nested in its own, it reads, is
stored and is returned as Nil. So never capture a `Value` in a lambda, a
callable or a deferred: convert it first (`AsString()`, `ToTransfer()`,
`ToCallable()`). Keeping one is a mistake, not a danger.

- **Kind.** `Kind()` is one of `Nil`, `Boolean`, `Number`, `String`,
  `Function`, `Array`, `Object`, `Bound`, `Other`. JavaScript `undefined` and
  `null` and Lua `nil` are all `Nil`. `IsNil()`, `IsBoolean()`, `IsNumber()`,
  `IsString()`, `IsFunction()`, `IsBound()` are the cheap checks.
- **`IsList()`** is the check for "a list was passed": an `Array`, or an
  `Object` without a key. The empty Lua table is an `Object`, as nothing tells
  it from one, so `IsArray()` is not that check. `Length()` and `At(i)` read
  whatever passes it, from zero in both languages; `Append(value)` adds.
- **`IsNull()`** tells JavaScript's `null` from `undefined`, for an API that
  gives `null` a meaning a value left out does not have (`attachTo: null`
  detaches a label). It is never true in Lua, whose tables cannot hold `nil`:
  where such an API takes `null` inside an object, a Lua script passes `false`
  and the function accepts it there.
- **Reading is strict.** `AsBoolean()` is true for `true` only, `AsNumber()` is
  0 for what is not a number, `AsString()` is empty for what is not a string,
  `AsInteger()` is empty unless the number is integral and fits 64 bits.
  `ToDisplayString()` spells any value as `String(value)` or `tostring(value)`
  does.
- **`AsBound<T>()`** is the object of a bound class in the value, or the `T`
  part of a derived one, and `nullptr` for anything else. It is how a handle
  argument is read: `call.Arg(0).AsBound<Entity>()` takes a `Player` too.
- **`Get(key)`, `Set(key, value)`, `Keys()`** work on plain objects and tables
  only. A bound instance and a function have no keys, in either language.
- **`Member(key)`** reads what a script reads as `value.key`, for a parameter
  that is "anything with an id": `Get` on an object or a table, the property on
  a bound object. In Lua that is a property its class declares for the runtime;
  in JavaScript whatever the object answers, a class a project bound by hand
  included.

A Lua table is read and written raw: no metamethod of the script runs inside
the host function, and a table whose metatable is protected, as that of a class
or a namespace is, is not written to. A JavaScript object is read and written
as a script would, so a getter, a setter or a proxy runs, and what it throws is
thrown by the call once the function has returned (`HasThrown()` tells).

`Keys()` gives integer keys ascending before string keys, and for an `Array`
its indices from zero as text.

## Kept functions

A script function kept beyond the call that received it, such as a handler, a
subscriber or a command callback, is taken with `Value::ToCallable()`, which
gives a `std::shared_ptr<Bind::ScriptCallable>` (`bind/callable.h`). Tables
that would have held a `v8::Global<v8::Function>` hold that instead.

```cpp
class ScriptCallable {
    virtual Language GetLanguage() const = 0;
    virtual std::string ResourceName() const = 0;
    virtual bool IsAlive() const = 0;
    virtual bool Equals(const ScriptCallable &other) const = 0;
    virtual void Invoke(const std::vector<TransferredValue> &args, ReturnMode mode, Completion done) = 0;
};
```

- A function has one callable for as long as anyone holds it, so taking it
  twice gives the same one and a handler can be found again to be removed.
- `Invoke` runs the function in its runtime with copies of `args`. `done`
  (`fu2::unique_function<void(CallOutcome &&)>`) runs exactly once, on the
  scripting thread: before `Invoke` returns when the function finished at once,
  later when it returned a promise (JavaScript) or waited (Lua). It must not
  throw. An empty `done` is one nobody waits on.
- `CallOutcome` has `threw`, `error` and `value`. `ReturnMode::Value` brings
  back a copy of the result; `ReturnMode::Outcome` only a boolean, a handler's
  veto, so a handler may return what cannot cross.
- The function is called on no object of the script: `this` is the global
  object of its context in JavaScript, and Lua gets no receiver.
- A callable is dead once its runtime is stopped: `IsAlive()` is false, and
  every `Invoke` it still owed, like every later one, is told `threw` with
  `Bind::kRuntimeStopped`. Drop a dead callable; never invoke it.
- A callable need not be a script function. The host may implement one for
  something a script is handed as a function, as the `reply` of a message
  handler is. Such a callable says `Language::Lua`.

`Bind::WatchRuntimes(engine)` is what has the callables and deferred results of
an engine told when a runtime stops. Call it once for each engine, before a
script runs in it; the scripting modules do.

### Owner and cleanup

A builtin that keeps a function names its owner with `call.OwnerOf(function)`,
never with `Call::ResourceName()`. In JavaScript that is what
`ResourceManager::ResolveResourceContext` answers for the function: the
resource of the runtime where each has its own, and in the one isolate of the
client the resource whose script made the function, before the one that is
calling. In Lua it is the resource of the runtime. This is `StateBag.onChange`
(`builtins/state_bag.cpp`):

```cpp
std::string resourceName = call.OwnerOf(call.Arg(1));
if (resourceName.empty()) {
    call.ThrowError("StateBag.onChange: must be called from within a resource");
    return;
}

std::shared_ptr<Bind::ScriptCallable> callback = call.Arg(1).ToCallable();
if (callback == nullptr) {
    return;
}
// ...
_subscriptions.emplace(id, Subscription {std::move(callback), std::move(resourceName), LinkOf(call.GetResourceManager()), handle});
```

The rules:

- **Refuse an empty owner.** Cleanup matches on the name, so an entry without
  one would survive every resource stop.
- **Keep the manager next to the name**, and match both when a resource stops
  (`StateBag::CleanupResource(manager, name)`), so two managers in one process
  cannot drop each other's entries. Drop everything of a manager when it goes
  (`StateBag::ForgetManager`).
- **Cover both languages.** Entries also go when their isolate goes
  (`UnregisterIsolate`) and when their Lua runtime goes
  (`DropDeadSubscriptions`, from the Lua engine's disposing listener).
- **Report a failed handler.** An outcome nobody reads is an error nobody
  sees. Give `Invoke` a `done` that reports with
  `ResourceManager::ReportHandlerFailure(owner, message)`, unless the error is
  `Bind::kRuntimeStopped`:

  ```cpp
  callback->Invoke(args, Bind::ReturnMode::Outcome, [manager = entry->second.manager, resourceName = entry->second.resourceName, key = change.key](Bind::CallOutcome &&outcome) {
      if (outcome.threw && outcome.error != Bind::kRuntimeStopped) {
          ReportFailure(manager->manager, resourceName, key, outcome.error);
      }
  });
  ```

  `ReportHandlerFailure` queues the report, and the manager acts on it at its
  next upkeep (`ProcessHandlerFailures`), only if the resource is still
  running. Never call `HandleResourceRuntimeError` from `done`: a script may be
  executing, or the resource stopping.
- **Expect re-entry.** A handler may call the builtin that invoked it, and
  `done` may invoke callables and destroy runtimes. Do not hold iterators or
  references into your own tables across `Invoke` or `done`, and check that an
  entry is still alive right before using it.
- **Copy arguments before consuming or collecting handlers**, so a failed copy
  changes nothing.

### The JavaScript fast path

Every callable whose language is JavaScript is a `Bind::V8Callable`
(`bind/v8_callable.h`), which gives its `Isolate()` and its `Function()` to a
caller that runs in the same isolate and needs no copy. Use it where
JavaScript semantics require passing values by reference to a JavaScript
handler in the same isolate, because scripts can observe object identity
today. Everything else copies through `TransferredValue`.

```cpp
v8::Isolate *isolate = callback->GetLanguage() == Language::JavaScript ? static_cast<const Bind::V8Callable &>(*callback).Isolate() : nullptr;
```

Only a callable that says `Language::JavaScript` may be taken for a
`V8Callable`. One that says `Language::Lua` is never downcast without asking
(`dynamic_cast`), because a callable of the host says so too. `Isolate()` is
`nullptr` and `Function()` is empty once the callable is dead.

`Events` is the worked example of a bus (`builtins/events.cpp`). A JavaScript
emit hands its arguments to the JavaScript handlers of its own isolate as they
are, with `Bind::V8ValueOf(call, value)`, and returns the Promise it always
returned with `Bind::V8ValueIn(call, promise)`. Every pair that involves Lua
goes through `ScriptCallable::Invoke` with copied arguments. These two
functions (`bind/v8_backend.h`) are the one place a context function speaks
V8; `Exports`, `Imports` and `Messages` use them the same way.

### Functions a builtin returns

`Call::NewFunction(fn)` makes a script function that calls `fn` with a `Call`
of its own, and keeps `fn` for as long as the script keeps the function:

```cpp
call.Return(call.NewFunction([id](Bind::Call &) {
    Unsubscribe(id);
}));
```

`Call::NewFunction(fn, data)` also carries one script value, which lives as
long as the function does, where the collector of the language sees it. The
function reads it with `Call::Data()`. It is how a returned function refers to
a script value without the host holding it. This is the unsubscribe function
of `Events.on`:

```cpp
call.Return(call.NewFunction(
    [self = binding.events->_self, scope, eventName, resourceName](Bind::Call &unsubscribe) {
        Events *events                                       = *self;
        const std::shared_ptr<Bind::ScriptCallable> callable = unsubscribe.Data().ToCallable();
        if (events != nullptr && callable != nullptr) {
            Remove(*events, scope, eventName, resourceName, *callable);
        }
    },
    call.Arg(1)));
```

Never capture a `shared_ptr<ScriptCallable>` strongly in such a closure. A
callable is a root: a handler that holds its own unsubscribe function would
then hold itself, and never be collected. Resolve the callable at call time
from the carried value.

## Asynchronous results

`Call::Defer()` makes the result of the call one that is settled later, and
gives a `Bind::Deferred`: a copyable handle with `Resolve(TransferredValue = {})`,
`Reject(std::string)`, `Settled()` and `Awaited()`. The first settlement
counts.

- In JavaScript the script gets a promise.
- In Lua the coroutine that called waits, and goes on with the value as the
  result of the call, or with the message raised from it. A deferred settled
  before the function returns makes the call return or raise at once.
- Where a Lua script cannot wait, on the main thread or in a coroutine it made
  itself, a call that is still unsettled when the function returns raises
  `cannot wait here`.
- What the function threw wins over what it deferred.

```cpp
Bind::Deferred deferred = call.Defer();
EmitCopied(events._resourceManager, collect(), args, eventName, [deferred, failureMessage](std::vector<std::string> &&errors) mutable {
    if (errors.empty()) {
        deferred.Resolve();
    }
    else {
        deferred.Reject(DescribeFailures(failureMessage, errors));
    }
});
```

A deferred may be captured and kept; a `Value` may not. `Awaited()` tells
whether a settlement would still reach a script, for a builtin that keeps
deferreds in a table and has to let go of them
(`Messages::DropAbandonedRequests`). Do not call the Lua scheduler from a
builtin: `Defer` does the waiting.

## Crossing runtimes

Every resource has its own runtime per language, so a value that goes from one
script to another is copied, as a `TransferredValue`
(`scripting/transferred_value.h`). `ValueTransfer` copies a JavaScript value
and `LuaTransfer` a Lua one. What each kind of value becomes is tabled in
[scripting_lua.md](scripting_lua.md).

In a context function:

| Call | Does |
|---|---|
| `Value::ToTransfer()` | Copies the value. A function crosses as its callable. |
| `Call::FromTransfer(value)` | Builds a copy in this runtime. A function arrives as one that invokes the original in its owner. |
| `Value::ToJson()` | The JSON text: `JSON.stringify` in JavaScript, where a `toJSON` is asked, `LuaJson::Encode` in Lua. |
| `Call::ParseJson(text)` | `JSON.parse` in JavaScript, `LuaJson::Decode` in Lua. |

All four return a `Utils::Result` and leave no pending throw, in either
language. The function decides what a value that does not cross means: it goes
on without it, or throws the error under its own name.

```cpp
auto copied = call.Arg(i).ToTransfer();
if (!copied) {
    call.ThrowTypeError(ArgumentError(api, i - first, copied.GetError()));
    return;
}
args.push_back(std::move(copied).GetValue());
```

`ToJson(bool &omitted)` tells the one failure where `JSON.stringify` gives
`undefined` instead of text, for an API that takes such a value for no value.

A definition says how its class leaves a runtime:

- **`Transfer(get)`**: an object crosses as itself, as the bytes of what `get`
  reads from it, and is built again as `T(payload)`. A value type crosses as
  its value (`.Transfer(&Vector3::vec)`), a handle as its id
  (`.Transfer(&Entity::GetId)`). The payload is trivially copyable. A
  constructor that throws, as that of a handle whose entity is gone, fails the
  copy with `what()`.
- **`Transfer(write, read)`**: for an object that crosses as more than one
  value, as a handle that carries text does (`PendingConnection`).
- **`RegisterTransfer()`** registers what `Transfer` declared, with
  `ValueTransfer` and `LuaTransfer`, under the name of the class. It is
  process-wide and for the start of the process. The newest registration is
  tried first, so a class is registered after its bases.
  `Builtins::RegisterTransferTypes()` does it for the Framework's types and
  must run before a project registers its own.
- **`JsonProperties({"x", "y", "z"})`**: the properties an object is written to
  JSON with from Lua, in that order. It names what `toJSON` gives in
  JavaScript, which stays a `V8Hook`. A bound object without them has no JSON
  form in Lua.

A Lua object whose class, and every base of it, declares no `Transfer` is
refused by a copy. A handle a project registered for JavaScript alone
(`Builtins::RegisterHandleTransfer<T>`) still crosses between JavaScript
runtimes, and is refused where it would arrive in Lua
(`host type 'Vehicle' is not available in Lua`).

## Sides

`Bind::Side` is `Shared`, `Server` or `Client`.

- **A definition declares the side it exists on**, as the last argument of its
  constructor, `Shared` by default. A `Shared` one is in every runtime. One of
  a side is in the runtimes of that side only: `RegisterLuaGlobal` and
  `MaterialiseLua` do nothing anywhere else, a namespace's `MaterialiseV8`
  returns an empty handle, and a class's `MaterialiseV8` throws, so its
  `Register` asks `Bind::ExistsIn(Describe(), runtime)` first.
- **Each member has a side**, the last argument of `Method`, `Static`,
  `Property`, `Function` and `Constant`. A runtime gets the `Shared` members
  plus its own: `Entity.setVirtualWorld` is `Bind::Side::Server`.
- **A base is `Shared` or of the side of the class** that inherits it.

A module may so register every definition it knows, in any order. Two
definitions may share a global name when no runtime has both, as the `Chat` of
the server and the `Chat` of the client do. `Bind::ApiCatalog::Find(name, side)`
gives the one a runtime of that side has; `Find(name)` gives the first
recorded.

The side of a runtime is `ScriptingSide(isolate)` in JavaScript and
`runtime.Options().side` in Lua. A context function asks `Call::GetSide()`.

## Per-language hooks

A definition names neither `v8::` nor `lua_`, except through a hook. A hook is
legitimate for what truly exists in one language only. Say why in a comment.

| Hook | For | Example |
|---|---|---|
| `V8Hook(fn)` | A member JavaScript alone has. Runs each time the class is materialised into an isolate, after its declared members. What it adds, it records in the catalog itself; `v8pp::class_::prototype_function` does both. | `toJSON`, the protocol of `JSON.stringify` |
| `V8Constructor(factory)` | A JavaScript constructor more lenient than its signature, such as one whose arguments default. The constructor stays documented as declared, and Lua keeps the declared one. | `new Color()` |
| `V8EnsureBase(ensure)` | Making the v8pp class of a base exist in the isolate before this one inherits from it: the `GetClass(isolate)` of the base. | |
| `LuaMeta(event, fn)` | A Lua operator: `Add`, `Sub`, `Mul`, `Div`, `Unm`, `Eq`, `Lt`, `Le`, `Len`, `Concat`. `fn` takes every operand. Several functions for one event are tried in order, which is how `vector * number` and `number * vector` coexist. | the arithmetic of the vectors |
| `LuaHook(hook)` | Anything else a Lua state alone needs. Runs each time the class is materialised into a state, with the indices of the metatable and of the class table. No builtin needs one today. | |

Lua idioms are added, never substituted: an operator comes beside the method
of the same meaning, and the method keeps its name.

Not legitimate: a hook for a member both languages could have, a second name
for an API in one language, or a behaviour that differs for no reason a script
author would recognise. What only one language's script can observe may also
be done in that language's registration, after the definition is materialised:
`console.info` is the function under `console.log` in JavaScript
(`Console::Register`).

## Differences between JavaScript and Lua

JavaScript gets a class through v8pp, unchanged, so that a binding ported to a
definition behaves as it did. In eight places that is not what the Lua backend
does with the same typed member (`bind/v8_backend.h`):

1. **A method that returns a reference or a pointer to another bound object.**
   JavaScript gets the object that already wraps it
   (`c.pick(other) === other`); Lua gets a copy.
2. **A method of a base that returns its receiver, called on an object of a
   derived class whose base is not the first one.** v8pp finds no wrapper at
   that address and returns a copy typed as the base, so the rest of a chain
   works on the copy. Lua returns the receiver. With single inheritance both
   return the receiver.
3. **A parameter that is a pointer to a bound class.** JavaScript passes
   `nullptr` for anything that is not such an object, so the function has to
   expect it; Lua refuses the call. A reference is refused by both.
4. **Errors.** v8pp's words do not name the function (`Argument count does not
   match function definition. Expected 1 but got 0`); Lua's do (`bad argument
   #1 to 'Counter:add' (integer expected, got string)`).
5. **A string parameter.** v8pp also takes a number and spells it; Lua takes a
   string only.
6. **An assignment to a property without a setter.** JavaScript ignores it, or
   throws a TypeError in strict mode; Lua raises (`attempt to set 'length' on
   Vector3`).
7. **A class with a `V8Constructor`.** JavaScript may accept what Lua refuses
   (`new Color()` is a color, `Color.new()` an error).
8. **A parameter that is a glm vector or quaternion.** JavaScript takes any
   object and reads the components it has (`{ x: 1, y: 2, z: 3 }`); Lua takes
   the userdata of the value type only.

Besides, v8pp cannot return an object of a class that cannot be copied, by
value or by reference: a class with such a member materialises into Lua only.

None of this is true of a context function. What still differs for one is
said above: `null` (`IsNull`), raw table access, the empty table, a wrong
receiver, and every error being a raised string in Lua.

## What a call costs

`FrameworkBindingBench` (`code/tests/bench/binding_bench.cpp`) times a bound
call from a script of each language, in real resources of a server scripting
module. An emission is timed until its one handler has run.

| Operation | JavaScript | Lua |
|---|---|---|
| `Vector3` constructed | 552 ns | 95 ns |
| `vector.add(other)` | 3370 ns | 109 ns |
| `vector.x` read | 1253 ns | 62 ns |
| `Events.emit`, handler in the same resource | 56 µs | 4.6 µs |

| `Events.emit` to another resource | Handler in JavaScript | Handler in Lua |
|---|---|---|
| Emitted from JavaScript | 60 µs | 6.8 µs |
| Emitted from Lua | 163 µs | 4.4 µs |

These numbers are indicative only: they come from one run of an unoptimised
build (no `CMAKE_BUILD_TYPE`) on Apple Silicon macOS, where the Framework's own
C++ is not optimised while V8 and Lua are, so they say more about the order of
magnitude than about either language. Run the benchmark in RelWithDebInfo or
Release before drawing a conclusion.

## The catalog and the definition files

**JavaScript.** `MaterialiseV8` records each definition in the v8pp catalog of
the isolate (`GetScriptingCatalog(isolate)`), the way a binding written by hand
does. The scripting module exports it as JSON to the file the environment
variable `FRAMEWORK_SCRIPTING_API_METADATA` names, and the TypeScript contract
is generated from that outside this repository. Its content must not change
for an API whose behaviour did not change. A class JavaScript has as plain
objects is recorded with `Bind::RecordV8DataType(catalog, desc)`
(`PendingConnection`).

**Lua.** Every definition is recorded in `Bind::ApiCatalog`.
`Bind::WriteLuaDefinitions(catalog, side)` (`bind/lua_definitions.h`) writes
the definition file of the Lua language server (`---@meta`) for one side:
every class and namespace that exists there, then the globals every state has
that no definition declares (`LuaRuntimeGlobals()`: the timers, `sleep`,
`spawn`, `print`, `require`). A function documented as returning a Promise is
`---@async` and returns what the promise settles with. The scripting module
writes it to the file `FRAMEWORK_SCRIPTING_LUA_DEFINITIONS` names. What it is
written from is the list the module publishes:
`ServerScriptingModule::LuaBuiltinDefinitions()` on the server, which is
`Builtins::SharedDefinitions({...})` plus the module's own.

**Golden fixtures.** Both outputs are committed and held by tests:

| File | Test module |
|---|---|
| `code/tests/fixtures/scripting_catalog_server.json` | `scripting_catalog_golden` |
| `code/tests/fixtures/scripting_catalog_client.json` | `client_catalog_golden` |
| `types/lua/server.d.lua` | `lua_parity` |
| `types/lua/client.d.lua` | `lua_client_parity` |

A change to a binding that is not meant to change its documentation leaves all
four untouched. A deliberate change is taken in by running the module once
with `FW_UPDATE_GOLDEN=1` and committing the result:

```bash
FW_UPDATE_GOLDEN=1 FW_TEST_FILTER=scripting_catalog_golden,lua_parity build/bin/FrameworkTests
FW_UPDATE_GOLDEN=1 FW_TEST_FILTER=client_catalog_golden,lua_client_parity build/bin/FrameworkClientScriptingTests
```

The two client modules run in `FrameworkClientScriptingTests`, so the first
command alone leaves the client fixtures as they were.

Read the diff before committing it: it is what script authors will see.

## Testing a binding

Tests are modules in `code/tests/modules/*_ut.h`, run by `FrameworkTests`
(server and shared) and `FrameworkClientScriptingTests` (client runtime).

- **Keep every existing JavaScript test of the builtin green, unmodified.**
- **Mirror them from Lua** in `lua_builtins_<name>_ut.h`: every function,
  every documented error, every cleanup path. Use a real `LuaEngine` and
  `LuaRuntime` with the builtin registered the way the scripting module
  registers it.
- **Add a cross-language case** where it makes sense: a value set from
  JavaScript read from Lua, a Lua handler fired by a JavaScript action.

`lua_builtins_chat_ut.h` shows the shape. One stage has a JavaScript runtime
and a Lua runtime, the builtin is given to both, and the same call is held
against both:

```cpp
inline void GiveChat(LuaBuiltinsPlayerTest::JsSide &js, LuaBuiltinsPlayerTest::LuaSide &lua) {
    js.js.Enter([](v8::Isolate *isolate, v8::Local<v8::Context> context) {
        Bind::Registrar registrar(isolate, context, context->Global(), Bind::Side::Server);
        Chat::Register(registrar, Bind::Languages::JavaScript);
    });
    Bind::Registrar registrar;
    Chat::Register(registrar, Bind::Languages::Lua);
    registrar.Lua().Apply(lua.Runtime());
}

IT("gives a Lua state the Chat of JavaScript, under the same names", {
    EQUALS(Members(Chat::Definition()), "sendToAll(),sendToPlayer()");

    LuaBuiltinsPlayerTest::Stage stage;
    EQUALS(stage.lua.ready, true);
    GiveChat(stage.js, stage.lua);

    EQUALS(stage.lua.Eval("type(Chat) .. ' ' .. type(Chat.sendToAll) .. ' ' .. type(Chat.sendToPlayer)"), "table function function");
    EQUALS(stage.js.Eval("typeof Chat + ' ' + typeof Chat.sendToAll + ' ' + typeof Chat.sendToPlayer"), "object function function");
    EQUALS(stage.js.Eval("Object.keys(Chat).join()"), "sendToAll,sendToPlayer");
    // ...
});
```

Other modules to copy from:

| Module | Shows |
|---|---|
| `bind_call_ut.h` | Context functions, `Value`, callables and deferred results from both languages, on test classes. |
| `value_types_parity_ut.h` | The same expression in JavaScript and in Lua against a typed class. |
| `lua_parity_ut.h`, `lua_client_parity_ut.h` | The parity gate. |
| `scripting_catalog_golden_ut.h` | The catalog fixture. |

A Lua test stage that keeps functions does what the module does:
`Bind::WatchRuntimes(engine)`, a disposing listener that drops what the
builtins keep, and `Bind::SetCallResourceManager(&runtime, manager)` in the
setup callback (`LuaBuiltinsPlayerTest::LuaSide`).

## The parity gate

`lua_parity` (server) and `lua_client_parity` (client) are the acceptance
gate. Each:

- walks the JavaScript catalog and requires every global and every member to
  exist in the Lua state of a resource of the same side;
- requires everything the Lua definition file declares to be in that state,
  and every global of the state to be declared;
- reports a builtin registered for one language alone
  (`LuaParity::OneSided`);
- holds the generated definition file against the committed one.

A binding added through a definition, registered in both languages and listed
in the module's definitions passes without a change to the test. One
registered for JavaScript alone fails it.

What JavaScript has and Lua does not is an explicit list, `Exemptions()` in
each test, each entry with its reason:

| Exempt | Reason |
|---|---|
| `queueMicrotask` | JavaScript's own queue. Lua has no promises: the scheduler resumes a coroutine that waits. |
| `StateBag` | Not a global of JavaScript either; its members are held to the gate. |
| `Vector2.toJSON`, `Vector3.toJSON`, `Vector4.toJSON`, `Quaternion.toJSON`, `Color.toJSON` | The protocol of `JSON.stringify`. Lua's encoder writes the same object from `JsonProperties`. |

The test also requires that every exemption was used, so a stale entry fails.
Adding one needs a reason a script author would accept: the member is a
protocol of the JavaScript language, not an API.

## Bindings of a project

A project built on the Framework binds through raw V8 or v8pp today. Those
bindings keep compiling and keep working, **for JavaScript only**: `Engine`,
`GetIsolate()`, `T::GetClass(isolate)`, `Events::EmitReserved*`,
`RegisterHandleTransfer<T>` and the v8pp catalog keep their signatures and
behaviour. Nothing a project binds by hand appears in Lua.

To expose an API to Lua, a project:

1. **Declares it through `Bind`**, as above: `FW_SCRIPT_CLASS` and a
   `Definition()`. `GetClass(isolate)` materialises from the definition, so
   JavaScript and the catalog do not change. A handle derived from `Player`
   or `Entity` declares `.Inherit<Player>()`.
2. **Gives it one `Register(Bind::Registrar &registrar, Bind::Languages
   languages = Bind::Languages::All)`**, as the builtins have (see
   [Registration](#registration)): `registrar.Global(Definition(), languages)`.
3. **Registers it in every Lua state**, through the hook of its side, with
   what a Lua-only registrar records:

   ```cpp
   Bind::Registrar registrar;
   MyApi::Register(registrar, Bind::Languages::Lua);
   module.AddLuaRuntimeSetup([bindings = registrar.Lua()](LuaRuntime &runtime) {
       bindings.Apply(runtime);
   });
   ```

   and in JavaScript with a registrar built for its isolate
   (`Bind::Registrar(isolate, context, context->Global(), side)`) and
   `Bind::Languages::JavaScript`. The hooks:
   - **Server:** `ServerScriptingModule::AddLuaRuntimeSetup(fu2::function<void(LuaRuntime &) const> setup)`
     (`integrations/server/scripting/module.h`). `setup` runs in every Lua
     state the server creates from then on, after the Framework's own builtins
     are in it. Call it before or after `Init`, and before the resources that
     need it start: a state that already exists is not given it.
   - **Client:** `ClientScriptingModule::AddLuaRuntimeSetup(...)`, of the same
     signature (`integrations/client/scripting/module.h`). It forwards to
     `LuaClientHost::AddRuntimeSetup` (`scripting/lua/lua_client_host.h`), the
     hook the client scripting module applies its own builtins through
     (`Builtins::RegisterClientBuiltins`): `setup` runs after the shared
     and the client builtins. Call it before a resource starts.

   A setup runs inside the setup callback of the Lua engine, which protects
   it, and that is where `LuaBindings::Apply` belongs.
4. **Declares how its handles cross**, with `.Transfer(&T::GetId)` and
   `Definition().RegisterTransfer()`, after `Builtins::RegisterTransferTypes()`
   and each derived class after its base. `RegisterHandleTransfer<T>` alone
   serves JavaScript only.
5. **Cleans up what it keeps** for both languages, as
   [Owner and cleanup](#owner-and-cleanup) says.

`ServerScriptingModule` (`integrations/server/scripting/module.h`) otherwise
offers publicly: `GetLuaEngine()`, `RegisterLuaBindings(LuaRuntime &)`, which
the Lua engine runs inside every runtime it creates, in a protected call, and
which runs the setups after the builtins, and
the static `LuaBuiltinDefinitions()`, the definitions of what the Framework
publishes on a server. The `sdkCallback` of `Init` registers JavaScript
bindings only.

A project that wants its API in a Lua definition file writes one from its own
list: `Bind::WriteLuaDefinitions` takes any `ApiCatalog`, and
`Builtins::SharedDefinitions({...})` gives the Framework's definitions followed
by the ones it is passed.
