# Changes for Existing JavaScript Resources

Lua support was added beside JavaScript (see
[`scripting_lua.md`](scripting_lua.md)). The JavaScript API kept its names,
signatures, error messages and TypeScript contract. The builtins were moved
onto a binding layer that serves both languages, and that changed the
behaviour listed here. Everything not listed is meant to behave as before.

Each line reads *before → after*. **Script** marks a change a script can
observe, **Operator** one that shows in logs, packaging or how a server
reacts to a failing resource.

## What to check first

Three changes can stop a resource that used to keep running. Each makes a
throwing handler a runtime error of its resource, so the manifest's
`errorBehavior` applies, and its default is `stop`:

- a `Key.bind` handler that throws (client);
- a `Web.on` handler that throws (client);
- a state bag `onChange` handler that throws.

A resource that relied on "logged and carry on" needs
`"errorBehavior": "continue"` in its manifest, or a `try`/`catch` in the
handler.

## Handler failures (all builtins)

- **Operator.** A handler failure that is reported against a resource is
  queued and handled at the resource manager's next upkeep, and only for a
  resource that is still running → it is never handled inside the call that
  woke the handler. It is logged as `Runtime error: <what failed>` and the
  resource then follows `errorBehavior`.
- **Script.** A function the host keeps and calls later through the binding
  layer (state bag, key and web-view handlers, and any function that crosses
  to or from Lua) is called with the global object of its own context as
  `this`. Events and message handlers between JavaScript resources are called
  as before.
- **Script.** A resource that stops itself, or calls `process.exit()`, and
  whose script goes on running: its handlers and exported functions held
  through the binding layer were callable until the runtime was freed at the
  next tick → they are dead at once (`runtime stopped`). Function references
  between two JavaScript runtimes keep their old rule.

## Logs

- **Operator.** Script error entries had several formats → every one is
  `[resource] ...`, and each further line of a stack or traceback also starts
  with `[resource] `.
- **Operator.** Control characters in script error text were logged raw →
  they become spaces.
- **Operator.** Error entries were unbounded → an entry is cut at 16384 bytes.
- **Operator.** Error entries were unlimited → at most 200 per resource per
  second, then `[resource] errors suppressed: more than 200 in a second`, and
  `[resource] N error(s) were suppressed` when the resource is heard again.
- **Operator.** `Failed to start resource X: ...` →
  `[X] Failed to start resource: ...`.
- **Operator.** `resourceStop for 'X' rejected; forcing cleanup: ...` →
  `[X] resourceStop rejected; forcing cleanup: ...`.
- **Operator.** `Resource 'X' continues without optional dependency 'D': ...`
  → `[X] Continues without optional dependency 'D': ...`.
- **Operator.** A JavaScript message handler error in the handler's own
  runtime was logged without its stack → with its stack.
- **Operator.** Client timer errors and unhandled errors had no resource
  prefix → `[resource] ` prefix.
- JavaScript `console` output is unchanged: raw and unbounded.

## `console`

- **Operator.** The resource in the `[resource]` prefix was taken from the
  current context, then the stack → from the resource that owns the isolate
  first, then the current context, then the stack. They differ only where the
  current context named another resource than the isolate's owner.
- **Script.** An object argument was written with whatever
  `globalThis.JSON.stringify` was at the time → with V8's own JSON
  serialiser. A script that replaced `JSON.stringify` no longer changes
  console output.
- **Script.** The number 2^63 exactly was formatted through an undefined
  conversion → it is written `9.22337e+18`.

## `Events`

- **Script.** `Events.emitServer` / `Events.emitAllClients` with a payload
  that has no JSON form (a cycle, a throwing `toJSON`, a function, a symbol):
  the serialiser's own exception was thrown unprefixed, or nothing was
  thrown, and the event could still be sent with an empty payload →
  `TypeError: Events.<function>: <reason>` and nothing is sent.
- **Script.** A handler registered by a script that goes on after its own
  resource stopped stayed in the table until cleanup → it is dead from the
  start: never called, dropped at the next emit or count.
- **Script.** `Events.listenerCount` counted handlers whose runtime had
  stopped → it does not.
- **Script.** New pairing: handlers written in Lua receive the events
  JavaScript emits, and the emit waits for them. They get copies of the
  arguments.
- **Operator.** While a lifecycle event (`resourceStart`, `resourceStop`)
  waits on a handler, only JavaScript was pumped → Lua timers are ticked too.
- **Host code.** `Events::ClearAll` / `Reset` called from inside a handler:
  the JavaScript handlers already picked for that emission were still called
  → they are skipped; the emission still settles.

## `Exports` / `Imports`

- **Script.** `Exports.get` / `Imports.get` found JavaScript exports only →
  they also find exports registered from Lua. `Imports.get` returns its
  object as before, plus one key per Lua-registered export.
- **Script.** In a mixed resource, `Exports.register` of a name a Lua script
  of the same resource already registered → throws
  `Error: Exports.register: export '<name>' is already registered`.
  JavaScript registering over its own JavaScript export still replaces it
  silently.
- **Host code.** `Exports::Register` / `Imports::Register` given a null
  manager: every call threw `resource manager not available` → the call uses
  the scripting module's manager when there is one, and throws that error
  otherwise.

## `Messages`

- **Script.** New pairing: a request or notification may be handled by a Lua
  handler. When it fails before replying, the Promise is rejected with an
  `Error` carrying the handler's message (a JavaScript handler in the
  requester's runtime still rejects with the thrown value). A payload that
  cannot be copied for a Lua handler rejects with
  `TypeError: Messages.request: payload: <why>` and the handler is not
  called; for `Messages.send` it is dropped with a log line.
- **Operator.** A handler failure nobody waits for (a notification, a
  failure after the reply) between two JavaScript resources is only logged,
  as before; when Lua is on either end it is a runtime error of the handler's
  resource.
- **Host code.** `Messages::Register` given a null manager: `handle` and
  `request` threw `resource manager not available` → they use the scripting
  module's manager when there is one.

## State bags (`entity.state`)

- **Script, Operator.** An `onChange` handler that threw synchronously was
  swallowed silently → it is a runtime error of its resource
  (`State change listener for '<key>' failed: <error>`), and `errorBehavior`
  applies.
- **Script.** `state.set(key, value)` with a value that has no JSON form (a
  function, a symbol) stored the text `undefined`, which read back as the
  string `"undefined"` → throws `TypeError: StateBag.set: ...` and stores
  nothing.
- **Script.** `state.set` with a cyclic value threw V8's `TypeError`
  unprefixed → the message starts with `StateBag.set: `. A `toJSON` that
  throws its own error class arrived as that object → it arrives as a
  `TypeError` carrying its message.
- **Script.** An `onChange` handler was called with `this` undefined → with
  the global object of its context.
- **Operator.** State change handlers were not dispatched when no scripting
  module was registered in `CoreModules` → they are.

## `TextLabel`, `Player`, `Chat` (server)

- **Script.** An options argument that is a *function* carrying properties
  (`Chat.sendToAll(text, fn)`, `TextLabel.create` / `update` options and
  their nested blocks) had its properties read → it is accepted and read as
  empty. The same holds for a bound object used as an options object.
- **Operator.** `TextLabel.create` on a host that wired no entity-ownership
  hook: nobody owned the label → the calling resource owns it, and it is
  destroyed when that resource stops. On the Framework's server, which wires
  the hook, nothing changes.

## `PendingConnection` and `Voice` (server)

- **Script.** The `PendingConnection` a `playerConnecting` handler receives
  was a writable copy → its properties are read-only and non-deletable, as
  the TypeScript contract always said. Assigning to one is ignored, and
  throws a `TypeError` in strict mode. Replacing `reject` on it does nothing.
- **Script.** `Voice.setPlayerMuted`, `setPlayerDeaf` and `setLocalMute`
  with the BigInt `0n` as the flag: false → true. Every other value reads as
  before.

## `Key` (client)

- **Script, Operator.** A key handler that threw synchronously was only
  logged (`[res] Key 'f5' (down) handler error: ...`) and the resource ran on
  → it is a runtime error of its resource and `errorBehavior` applies. See
  `docs/scripting_keybindings.md`.
- **Script.** After one key handler of a resource failed, its other handlers
  still ran in that frame → they are skipped until the next update. Handlers
  of other resources run.
- **Script.** `Object.keys(Key)` was `bind, unbind, isDown, getLabel` →
  `getLabel, bind, unbind, isDown`.

## `Web` (client)

- **Script, Operator.** A `Web.on` handler that threw was only logged → it is
  a runtime error of its resource and `errorBehavior` applies (`stop`
  destroys the resource's views). See `docs/scripting_web_events.md`.
- **Script.** After one `Web.on` handler of a resource failed, its other
  handlers still ran for every event → they are skipped until the next
  scripting update.
- **Script.** A handler was still called for an event that arrived before its
  resource stopped, also when an earlier handler of the same event stopped it
  → a handler is not called once its resource has stopped.
- **Script.** A lone surrogate in a URL or an event name reached the view as
  bytes that are not UTF-8 → as U+FFFD.
- **Script.** A function passed as `Web.createView` options had its
  properties read → it is accepted and not read.
- **Script.** A getter that throws inside `Web.createView` options → the
  exception is thrown to the script and no view is created.

## `Discord` (client)

- **Script.** A getter that throws inside the options object of
  `Discord.setAssets`, `setParty`, `setSecrets` or `setPresence` was usually
  lost and the call returned normally → the exception is thrown by the call,
  and `setPresence` does not publish when it throws.
- **Script.** `party.size` elements that are objects were coerced by
  JavaScript (`[[5], 2]`, a `valueOf`) → they read as 0. A string in a
  non-decimal notation (`'0x10'`, `'0b11'`, `'0o7'`) → 0. Numbers, booleans
  and decimal strings read as before.
- **Script.** A function passed as an options object had its properties read
  → it is accepted and not read.

## `Nametags` (client)

- **Script.** The colour of `Nametags.setLabel` given a negative or too large
  number was an undefined conversion → it is wrapped to 32 bits, and is 0 for
  NaN and the infinities.

## Values crossing between client resources

- **Script.** On the client, a value type or handle (`Vector3`, `Color`,
  `Player`, ...) sent through `Messages` or `Exports` to another resource
  arrived as a plain object → it arrives as itself, as on the server. Client
  events between JavaScript handlers pass values by reference and are
  unchanged.

## Packaging and tooling

- **Operator.** A resource that declares no `files`: the scan of the client
  and shared script directories shipped `.js` and asset files → it also ships
  `.lua` files, except those named in `serverScripts`. A server-side `.lua`
  helper in such a directory that is not listed in `serverScripts` ships.
  Declare `files` to control what ships.
- **Operator.** New: a script whose name ends in `.lua` runs in Lua. On a host
  without a Lua engine the resource fails to start with
  `Lua scripting is not available`.
- **Tooling.** In the exported API catalog, `toJSON` of the value classes now
  follows their static members. The content is the same; the member order in
  a generated `.d.ts` moves.

## Not changed

Listed because they were questioned during the work and settled as "stays":

- `console.log === console.info` is still `true`.
- The function `Events.on` / `Events.onClient` returns still removes every
  registration of that handler for the event, whenever it is called.
- `Web.emit` with a payload that is not JSON-serializable still throws
  `Web.emit: payload is not JSON-serializable`.
- A `PendingConnection` can still be handed to another resource, pending or
  decided.
- Dispatch between JavaScript handlers in one runtime still passes objects by
  reference; between JavaScript resources, exported methods still run with
  the owner's object as `this`.
- Error classes, messages and the order of argument checks of every ported
  function.
