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
| Wrong argument count, type, or shape | `isolate->ThrowException(v8::Exception::TypeError(...))` |
| Valid call, but the world can't satisfy it (subsystem missing, resource not running, limit reached, no handler, cross-isolate) | `isolate->ThrowException(v8::Exception::Error(...))` |
| A stale handle to a destroyed/absent object | silent no-op (return without throwing) |

Rules:

- **Never** use `isolate->ThrowError(...)`. It is equivalent to
  `ThrowException(Exception::Error(...))` but hides the Error-vs-TypeError choice
  above. Always spell out `ThrowException(Exception::Error/TypeError(...))`.
- **Silent no-op is only for stale handles.** A bad argument is a programming
  error and must throw; swallowing it hides the caller's bug. If two builtins
  face the same bad-argument case, they must both throw (e.g. `Chat.sendToPlayer`
  and `Entity.setVisibleTo` both reject a non-player argument).
- **A missing subsystem is a state error, not a stale handle** — throw. Do not
  silently drop the call (e.g. `Chat.send` with no network peer throws, matching
  `TextLabel.create`).
- Message format: `"<Api>.<method>: <what was expected / what went wrong>"`.

## Registration

There are two legitimate `Register` shapes; a builtin uses the one that matches
what it needs, and nothing else diverges:

- **Value / handle types** — pure v8pp classes attached to a target object:

  ```cpp
  static void Register(v8::Isolate *isolate, v8::Local<v8::Object> target);
  ```

  These need nothing from the runtime. The pure value types
  (`Vector2/3/4`, `Quaternion`, `Color`) are registered together by
  `Builtins::RegisterValueTypes`. Handle types (`Entity`, `Player`, `TextLabel`)
  register individually because they pull in networking/replication and are
  wired up by the integration layer, not by the value-type helper.

- **Service APIs** — need the context and the `ResourceManager`:

  ```cpp
  static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context,
                       v8::Local<v8::Object> target, ResourceManager *manager);
  ```

  `Events` is the one deliberate variation: it is a non-static member of
  `ResourceManager` (which owns the handler tables), so it is called as
  `manager.GetEvents().Register(...)` with the same argument list.

`RegisterValueTypes` registers **only** the pure value types — its name says so.
It is the counterpart to `UnregisterAll`, which additionally drops the handle
types' cached class wrappers on isolate disposal.
