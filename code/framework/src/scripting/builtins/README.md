# Scripting builtins — conventions

Every builtin under `scripting/builtins/` follows the rules below. They exist so
that scripts see one consistent surface: the same failure always fails the same
way, and the same kind of builtin always registers the same way. When you add or
edit a builtin, match these. When something here is wrong, fix the rule and sweep
the code — don't let the code drift silently.

## Namespace

All builtins live in `Framework::Scripting::Builtins`. No exceptions — value
types (`Vector3`, `Color`, …), handle types (`Entity`, `Player`, `TextLabel`),
and service APIs (`Console`, `Environment`, `Events`, `Exports`, `Imports`,
`Messages`, `Chat`) all share the namespace. A builtin that needs a type from
another subsystem forward-declares it in its own namespace; it does not move
itself out of `Builtins` to be near that type.

## Throwing

Pick the failure mode by *what kind* of thing went wrong — not by which file you
happen to be in:

| Situation | Idiom |
|---|---|
| Wrong argument count, type, or shape | `ThrowTypeError(isolate, ...)` (`scripting/engine_helpers.h`) |
| Valid call, but the world can't satisfy it (subsystem missing, resource not running, limit reached, no handler, cross-isolate, argument's object has no live connection) | `isolate->ThrowException(v8::Exception::Error(...))` |
| A method called on a stale **receiver** (`this` whose object is gone) | silent no-op (return without throwing) |

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
- **A missing subsystem is a state error, not a stale handle** — throw. Do not
  silently drop the call (e.g. `Chat.send` with no network peer throws, matching
  `TextLabel.create`).
- Message format: `"<Api>.<method>: <what was expected / what went wrong>"`.

## Registration

`target` is always the context's global root — there is no namespace object to
nest under, so every builtin is reachable as a bare global (`Web`, `Key`,
`Exports`, …). Names that Node's CommonJS module scope would shadow are
capitalized for that reason: `Exports`, not `exports`.

There are two legitimate `Register` shapes; a builtin uses the one that matches
what it needs, and nothing else diverges:

- **Value / handle types** — v8pp classes attached to a target object:

  ```cpp
  static void Register(v8::Isolate *isolate, v8::Local<v8::Object> target);
  ```

  These need nothing from the runtime. The pure value types
  (`Vector2/3/4`, `Quaternion`, `Color`) are registered together by
  `Builtins::RegisterValueTypes`. Handle types (`Entity`, `Player`, `TextLabel`)
  register individually because they pull in networking/replication and are
  wired up by the integration layer, not by the value-type helper.

  The value types are declared once for JavaScript and Lua, through the binding
  layer (`scripting/bind/`), and each has the same three entry points:

  ```cpp
  static const Bind::Class<T> &Definition();
  static void Register(v8::Isolate *isolate, v8::Local<v8::Object> target);
  static void RegisterLua(lua_State *L, Bind::Side side);
  ```

  `Definition()` holds the members and their documentation. `GetClass(isolate)`
  builds the v8pp class from it, so `Register` and the catalog follow the
  definition; `RegisterLua` publishes the same class as a global of a Lua state,
  and `Builtins::RegisterValueTypesLua` does so for the five. A new member goes
  in the definition, never in one language's registration. What exists in one
  language only is declared there too: `toJSON` is JavaScript's (`V8Hook`), the
  arithmetic and equality operators are Lua's (`LuaMeta`). The definition also
  says how the type leaves a runtime: `JsonProperties` names what Lua writes it
  to JSON with, the same keys and values `toJSON` gives, and `Transfer` what it
  crosses to another runtime as, which `Builtins::RegisterTransferTypes`
  registers for both languages.

  `Entity` and `StateBag` are declared the same way. A handle's `RegisterLua`
  takes the `LuaRuntime`, as a service API's does, and the scripting module
  calls `Entity::RegisterLua` for every state it sets up. `StateBag` is a
  global in neither language: it is materialised for `entity.state` to give,
  and not published. Both cross between runtimes by their network id
  (`Transfer(&Entity::GetId)`); a handle a project derives by hand from
  `Player` or `Entity` still registers through `RegisterHandleTransfer`.
  A function `StateBag.onChange` keeps is a `Bind::ScriptCallable`, dropped
  when its resource stops (`StateBag::CleanupResource`), when its isolate
  goes (`UnregisterIsolate`) and when its Lua runtime goes
  (`DropDeadSubscriptions`, from the Lua engine's disposing listener).

- **Service APIs** — need the context and the `ResourceManager`:

  ```cpp
  static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> target, ResourceManager *manager);
  ```

  `Events` is the one deliberate variation: it is a non-static member of
  `ResourceManager` (which owns the handler tables), so it is called as
  `manager.GetEvents().Register(...)` with the same argument list.

  A service API that is declared once for JavaScript and Lua goes through
  `Bind::Namespace` (`scripting/bind/namespace.h`). `Console` and
  `ExecutionEnvironment` are the worked examples. Each keeps its `Register`
  and has two entry points more:

  ```cpp
  static const Bind::Namespace &Definition();
  static void RegisterLua(LuaRuntime &runtime /*, what Register is told */);
  ```

  - `Definition()` holds the functions, the constants and their
    documentation. A function is a context function, `void(Bind::Call &)`: one
    body for both languages, which names neither.
  - `Register` materialises the definition into the isolate
    (`Definition().MaterialiseV8(...)`), which sets the global and records the
    catalog; `RegisterLua` publishes the same definition as a global of a Lua
    state (`Definition().RegisterLuaGlobal(...)`), and the scripting module
    calls it for every state it sets up.
  - A function made from a definition carries no `ResourceManager`. It asks
    its call: `Call::ResourceName()`, `Call::GetResourceManager()`. So a
    `Register` that is given a manager tells the binding layer which one the
    isolate answers with (`Bind::SetCallResourceManager`); the layer forgets
    it when the isolate is unregistered or the manager is destroyed.
  - A value that is only known when a runtime is set up (the side, the mod
    version) is a `RuntimeConstant` of the definition, and `Register` and
    `RegisterLua` hand its value to the materialisation.
  - What a script writes goes through `Call::WriteOutput`: the scripting
    logger in JavaScript, the path of `print()` in Lua. A line built from
    many values stops at `Call::OutputLineLimit()`, as the console does: what
    a Lua script passes is not bounded by the memory cap of its state once
    the host copies it.
  - What only one language's script can observe is done in that language's
    registration, after the definition is materialised: `console.info` is the
    function under `console.log` in JavaScript, as it was before the port.

`RegisterValueTypes` registers **only** the pure value types — its name says so.
It is the counterpart to `UnregisterAll`, which additionally drops the handle
types' cached class wrappers on isolate disposal. `RegisterValueTypesLua` has no
such counterpart: closing a Lua state drops everything registered in it.

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
