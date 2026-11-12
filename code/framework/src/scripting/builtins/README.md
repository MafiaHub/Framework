# Scripting builtins — conventions

Every builtin under `scripting/builtins/` follows the rules below. They exist so
that scripts see one consistent surface, in JavaScript and in Lua: the same
failure always fails the same way, and the same kind of builtin always registers
the same way. When you add or edit a builtin, match these. When something here
is wrong, fix the rule and sweep the code — don't let the code drift silently.

This file is normative for what a builtin does. How the binding layer works is
in [docs/scripting_bindings.md](../../../../../docs/scripting_bindings.md), and
what a Lua script author sees is in
[docs/scripting_lua.md](../../../../../docs/scripting_lua.md).

## Namespace

All builtins live in `Framework::Scripting::Builtins`. No exceptions — value
types (`Vector3`, `Color`, …), handle types (`Entity`, `Player`, `TextLabel`),
and service APIs (`Console`, `ExecutionEnvironment`, `Events`, `Exports`, `Imports`,
`Messages`, `Chat`) all share the namespace. A builtin that needs a type from
another subsystem forward-declares it in its own namespace; it does not move
itself out of `Builtins` to be near that type.

## One definition for both languages

A builtin is declared once, through the binding layer (`scripting/bind/`), and
that definition serves JavaScript and Lua:

- `Definition()` holds the members and their documentation: a
  `Bind::Class<T>` for a value or handle type, a `Bind::Namespace` for a
  service API.
- A new member goes in the definition, never in one language's registration.
- Names are identical in both languages. Lua idioms (operators, `:` calls) are
  added, never substituted: instance methods are called with `:`, statics and
  namespace functions with `.`.
- Where a C++ signature says everything, a member is a typed callable. Where
  arguments are dynamic, it is a context function, `void(Bind::Call &)`: one
  body for both languages, which names neither.
- Nothing in a definition or its implementation names `v8::` or `lua_`, except
  through a hook for what exists in one language only, declared in the
  definition too: `toJSON` is JavaScript's (`V8Hook`), the arithmetic and
  equality operators are Lua's (`LuaMeta`).
- What only one language's script can observe may be done in that language's
  registration, after the definition is materialised: `console.info` is the
  function under `console.log` in JavaScript, as it was before the port.

## Throwing

Pick the failure mode by *what kind* of thing went wrong — not by which file you
happen to be in:

| Situation | In a context function | In a function that speaks V8 |
|---|---|---|
| Wrong argument count, type, or shape | `call.ThrowTypeError(...)` | `ThrowTypeError(isolate, ...)` (`scripting/engine_helpers.h`) |
| Valid call, but the world can't satisfy it (subsystem missing, resource not running, limit reached, no handler, cross-isolate, argument's object has no live connection) | `call.ThrowError(...)` | `isolate->ThrowException(v8::Exception::Error(...))` |
| A method called on a stale **receiver** (`this` whose object is gone) | silent no-op (return without throwing) | silent no-op |

In Lua a TypeError and an Error are both a raised string, with the same
message.

Rules:

- **Never** use `isolate->ThrowError(...)`. It is equivalent to
  `ThrowException(Exception::Error(...))` but hides the Error-vs-TypeError choice
  above. Use `ThrowTypeError` for a TypeError and spell out `ThrowException(Exception::Error(...))` otherwise.
- **Silent no-op is only for a stale receiver.** A method invoked on a handle
  whose entity was destroyed returns quietly (common during async teardown; don't
  crash the caller). Everything the *caller passes in* is different: a bad or
  stale **argument** is the caller's bug and must throw. If two builtins face the
  same bad-argument case, they must both throw — e.g. `Chat.sendToPlayer` and
  `Entity.setVisibleTo` both reject a non-player argument (`TypeError`) and a
  player with no owning connection (`Error`).
- **A wrong receiver is not a stale one in Lua.** A method or a property of a
  bound class called on anything but an object of the class raises
  `bad receiver for 'Class:method' (...)`, typed or context function alike:
  `connection.reject("x")` with `.` does not silently do nothing. Only a
  receiver of the class whose object is gone is a silent no-op. JavaScript
  returns silently for a wrong `this`.
- **A missing subsystem is a state error, not a stale handle** — throw. Do not
  silently drop the call (e.g. `Chat.send` with no network peer throws, matching
  `TextLabel.create`).
- **A number outside what the function takes**, where the API always threw a
  `RangeError`, is `Call::ThrowRangeError`: `Voice` throws one for a tier that
  is none. In Lua it is the raised string every error is.
- Message format: `"<Api>.<method>: <what was expected / what went wrong>"`.
  The client `Discord` keeps two messages from before this rule, which its
  single-field setters share and scripts may match: `Discord: expected a string
  argument` and `Discord: expected a numeric argument`. Do not add others.

## Registration

Every builtin has one entry point, which registers it in both languages:

```cpp
static void Register(Bind::Registrar &registrar,
                     Bind::Languages languages = Bind::Languages::All
                     /*, what it needs: ResourceManager *, modVersion */);
```

`languages` turns a language off (`Bind::Languages::JavaScript`,
`Bind::Languages::Lua`). A builtin never names Lua itself: it only includes
`bind/` headers, and does its work through the registrar
(`bind/registrar.h`):

- `registrar.Global(Definition(), languages)` publishes a `Bind::Namespace`
  or a `Bind::Class` as a global; `registrar.Type(Definition(), languages)`
  makes a class known without a global.
- JavaScript is registered at once, in the isolate the registrar was built
  for. Lua is recorded (`registrar.Lua()`), and the scripting module applies
  the record to every Lua state as it is created, with the side of that
  runtime: a state is made per resource, long after the builtins register.
- What a builtin keeps for its isolate (a handler table, the manager the
  isolate answers with, catalog metadata) goes under
  `if (registrar.HasJavaScript(languages))`, with `registrar.Isolate()`,
  `Context()` and `Target()`.

`Target()` is always the context's global root — there is no namespace object
to nest under, so every builtin is reachable as a bare global (`Web`, `Key`,
`Exports`, …), under the same name in a Lua state. Names that Node's CommonJS
module scope would shadow are capitalized for that reason: `Exports`, not
`exports`.

The scripting modules build one registrar per isolate
(`ServerScriptingModule::RegisterFrameworkBindings`,
`ClientScriptingModule::RegisterFrameworkBindings`) and keep its Lua record
for the states they create. `Builtins::RegisterShared` registers what both
sides have; a client's Lua host registers it in Lua itself
(`LuaClientHost::Init`).

### Value and handle types

Classes published as globals:

```cpp
static const Bind::Class<T> &Definition();
static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All);
```

`GetClass(isolate)` builds the v8pp class from the definition, so `Register`
(through `registrar.Global`) and the catalog follow it; in Lua the same
definition is published as a global of each state.

- **The pure value types** (`Vector2/3/4`, `Quaternion`, `Color`) need nothing
  from the runtime. They are registered together by
  `Builtins::RegisterValueTypes`.
- **`RegisterValueTypes` registers only the pure value types** — its name says
  so. It is the counterpart to `UnregisterAll`, which additionally drops the
  handle types' cached class wrappers on isolate disposal. Lua has no such
  counterpart: closing a Lua state drops everything registered in it.
- **Handle types** (`Entity`, `Player`, `TextLabel`) register individually
  because they pull in networking/replication and are wired up by the
  integration layer, not by the value-type helper. A handle's `Register`
  makes the classes it reads as known first (`registrar.Type`), as `Entity`
  does for `Vector3`, `Quaternion` and `StateBag`.
- **`Player` and `TextLabel` inherit `Entity`** in both languages
  (`Inherit<Entity>()`): a Lua `Player` passes where an `Entity` is expected.
  What only the server has of a player (`kick`, `emit`, the nametag setters,
  the identity of the connection) is tagged `Bind::Side::Server`. In
  JavaScript a game publishes the two itself (`Player::Register`,
  `TextLabel::Register`); a Lua state gets both from its scripting module,
  which registers them with `Bind::Languages::Lua` (`Builtins::RegisterShared`).
- **`StateBag` is a global in neither language**: it is materialised for
  `entity.state` to give, and not published.

How a type leaves a runtime is part of its definition:

- `JsonProperties` names what Lua writes it to JSON with, the same keys and
  values `toJSON` gives in JavaScript.
- `Transfer` says what it crosses to another runtime as, which
  `Builtins::RegisterTransferTypes` registers for both languages. A value type
  crosses as its value; `Entity` and `StateBag` cross by their network id
  (`Transfer(&Entity::GetId)`).
- A handle a project derives by hand from `Player` or `Entity` still registers
  through `RegisterHandleTransfer`, which serves JavaScript only.

### Service APIs

These are a `Bind::Namespace` and take the `ResourceManager`:

```cpp
static const Bind::Namespace &Definition();
static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All,
                     ResourceManager *resourceManager = nullptr);
```

`Exports`, `Imports`, `Messages` and `Console` have exactly this shape. A
service API takes only what it needs, so the trailing arguments vary:

```cpp
// ExecutionEnvironment: no manager, the values of its runtime constants
static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All, const std::string &modVersion = {});
// Chat: no manager
static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All);
```

`Console` and `ExecutionEnvironment` are the worked examples of a definition,
and `Chat` the one that reads an option object and a handle argument.

- `Definition()` holds the functions, the constants and their documentation.
- `Register` publishes the definition (`registrar.Global(Definition(),
  languages)`): in the isolate it sets the global and records the catalog,
  and every Lua state the module sets up gets the same definition as a
  global. JavaScript-only extras, such as `console.info` being
  `console.log`, use the object `Global` returns.
- A function made from a definition carries no `ResourceManager`. It asks
  its call: `Call::ResourceName()`, `Call::GetResourceManager()`. So a
  `Register` that is given a manager tells the binding layer which one the
  isolate answers with (`Bind::SetCallResourceManager`); the layer forgets
  it when the isolate is unregistered or the manager is destroyed. A Lua
  runtime is told its manager by the module that sets it up.
- A value that is only known when a runtime is set up (the side, the mod
  version) is a `RuntimeConstant` of the definition, and `Register` hands
  `registrar.Global` its value for each side.
- What a script writes goes through `Call::WriteOutput`: the scripting
  logger in JavaScript, the path of `print()` in Lua. A line built from
  many values stops at `Call::OutputLineLimit()`, as the console does: what
  a Lua script passes is not bounded by the memory cap of its state once
  the host copies it.

`Events` is the one deliberate variation in shape: it is a non-static member of
`ResourceManager` (which owns the handler tables), so it is called as
`manager.GetEvents().Register(registrar, languages, &manager)`. Its functions
come from one definition for JavaScript and Lua all the same
(`Events::Definition()`): a JavaScript call finds its `Events` by the isolate
`Register` bound, a Lua call through the manager of its runtime.

## Script functions a builtin keeps

Every builtin that keeps a script function beyond the call (a handler, a
subscriber, a command callback) follows these rules. `StateBag.onChange` is the
example.

- **It is a `Bind::ScriptCallable`**, taken with `Value::ToCallable()`.
- **Its owner is the resource `Call::OwnerOf(function)` names**, and it is kept
  with that resource's manager (`Call::GetResourceManager()`). An owner that
  cannot be named is refused: `"<Api>.<method>: must be called from within a
  resource"`.
- **It is dropped on every path, in both languages**: when the manager stops
  the resource (`StateBag::CleanupResource(manager, name)`), when the manager
  goes (`ForgetManager`), when its isolate goes (`UnregisterIsolate`) and when
  its Lua runtime goes (`DropDeadSubscriptions`, from the Lua engine's
  disposing listener). A dead callable (`IsAlive() == false`) is dropped,
  never invoked.
- **A handler that fails does not fail what woke it.** One that throws, or
  whose promise or coroutine fails later, is reported with
  `ResourceManager::ReportHandlerFailure`, which queues it, and becomes an
  error of its resource at the manager's next upkeep if the resource is still
  running. Never `HandleResourceRuntimeError` from the handler's `done`: a
  script may be executing, or the resource stopping.
- **Script text is logged through `LogScriptError(manager, resource, text)`**
  (`scripting/script_log.h`), as the manager does, by a builtin that logs what
  a handler failed with itself: the text is the script's, so it is cut to
  16384 bytes, control characters are blanked, every line is put under the
  name of the resource, and a resource gets 200 entries a second. No builtin
  and no engine writes an error of a script to the logger directly. A name a
  script chose (an event, a message type, an export) goes into any other log
  line through `BlankedLine`.

### Where a builtin speaks V8

Dispatch between two JavaScript scripts keeps the code it always ran, because
scripts can observe it. Whatever involves Lua goes through the handler's
`ScriptCallable` with copied arguments.

- **`Events`** is the one place where a context function speaks V8
  (`Bind::V8ValueOf`, `Bind::V8ValueIn`): a JavaScript emit hands its
  arguments to the JavaScript handlers of its own isolate as they are, and
  returns the Promise it always returned.
- **`Exports` and `Imports`** speak V8 where `Events` does: a JavaScript export
  stays a `v8::Global` of its resource (`Resource::RegisterExport`) and
  reaches a JavaScript importer by reference or through
  `FunctionReferences`, as before. An export registered from Lua is kept as a
  `TransferredValue` (`Resource::GetExportCopy`), and a Lua importer of a
  JavaScript export is given a copy made inside the owner's isolate
  (`Exports::CopyJavaScriptExport`); the functions in either are
  `ScriptCallable`s.
- **`Messages`**: a JavaScript request or notification to a JavaScript handler
  runs the code it always ran (`CallHandler`, the reply function, the response
  queue of `v8::Global`s). Where the requester or the handler is Lua, the
  handler is called through its `ScriptCallable` with a copy of the payload
  and `reply` is a callable of the host (`Messages::ReplyFunction`) that
  queues a copy of the answer; a Lua requester waits on a `Bind::Deferred`.
  `ProcessPendingResponses()` delivers both kinds of reply, with or without an
  isolate at hand. A handler is kept with the manager it was registered
  under, which is who a failure nobody waits for is reported to, and is
  dropped when that manager goes (`ForgetManager`). A request made from Lua
  that nobody waits for any more, because its runtime went or its call could
  not wait, is dropped by `DropAbandonedRequests`.

## Reading more of a value than its kind

`TextLabel` shows what a context function does where an API reads more of a
value than its kind:

- **`null` with a meaning.** `Value::IsNull()` tells JavaScript's `null`
  from `undefined`, which are both `Nil`: `attachTo: null` detaches and
  `showTo: null` is everyone, while a key left out changes nothing. Lua has
  no `null` and a table cannot hold `nil`, so such an option takes `false`
  from Lua (`showTo = false`).
- **`null` as an answer** (`getOptions()` of a destroyed label, the
  `attachTo` of a label that follows nothing) is `Call::Null()`: no value,
  and no key, in Lua.
- **"Anything with an id".** `Value::Member("id")` reads a member the way
  a script's `value.id` does: the key of a plain object or table, the
  property of a bound object of any class, a project's own included.
  `Get` reads plain objects and tables only.
- **A flag** is read as the language of the script reads a condition, as
  `Voice` reads its own.
- **Ownership.** A resource owns the labels it creates, and they are
  destroyed when it stops. `TextLabel.create` names the owner from its
  call (`ResourceManager::AssignEntity`), because the manager's own hook
  on entity creation names it from the running JavaScript and so misses a
  Lua script outside the load of its file.

## Server builtins

The server's `Voice` and `PendingConnection`
(`integrations/server/scripting/builtins/`) are declared through the binding
layer too.

`PendingConnection` is a `Bind::Class` that only Lua materialises: to
JavaScript a connection stays the plain object it was, built by `Create` with
functions that run the same context functions as the methods of the class, and
its catalog data type is written from `Describe()` (`Bind::RecordV8DataType`).
It crosses between runtimes with its identity (`Transfer(&Encode, &Decode)`,
`PendingConnection::RegisterTransfer`), so one the gate has decided still
arrives, as an inert handle; the JavaScript object carries the same bytes under
a private key.

## Color representations

Color is not stored one way across the surface — there are four representations,
because the packed integer forms are dictated by the wire/replication payloads
they feed, which we don't get to renumber freely. Know which one an API wants:

| Where | Representation |
|---|---|
| `Color` internal (`r`/`g`/`b`/`a`, `vec()`) | floats in `[0, 1]` |
| `Color.fromRGB(r, g, b, a?)` | byte components `0–255` |
| `Chat` message `color` option | packed `0xRRGGBBAA` (uint32) |
| `TextLabel` `color` / `setColor` | packed **ARGB** `0xAARRGGBB` (uint32) |

The two packed orders differ (`RRGGBBAA` vs `AARRGGBB`) because they mirror
different existing payloads; unifying them is a wire-format change, out of scope
for a scripting-layer bump.

To spare scripts from packing bytes in the right order, **the packed-int APIs
also accept a `Color`**: `Chat.send(text, { color: new Color(...) })`
and `label.setColor(new Color(...))` each convert the Color into that API's
own packed layout. Prefer passing a `Color`; reach for the raw packed int only
when you already have one.
