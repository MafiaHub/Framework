# Lua Resources

A resource can be written in Lua, in JavaScript, or in both. The Lua API has
the names the JavaScript API has: `Events.on`, `Exports.register`,
`Vector3`, `player:kick(...)`. This document is for resource authors. What
changed for existing JavaScript resources is in
[`scripting_javascript_changes.md`](scripting_javascript_changes.md).

## A Lua resource

The manifest is the same `package.json`. A script whose name ends in `.lua`
runs in Lua; every other script runs in JavaScript, as before.

```json
{
  "name": "lua-example",
  "version": "1.0.0",
  "description": "A Lua resource: events, timers, an export and a Vector3.",
  "mafiahub": {
    "serverScripts": ["server.lua"],
    "exports": ["distanceToSpawn"]
  }
}
```

```lua
-- A Lua resource. Every global here has the name it has in JavaScript.
local spawn = Vector3.new(10, 0, 5)
local greeted = 0

-- Other resources reach this through Imports.get('lua-example'), whatever
-- language they are written in.
Exports.register('distanceToSpawn', function(position)
    return spawn:distance(position)
end)

Events.on('example:hello', function(who)
    greeted = greeted + 1
    -- A table arrives in JavaScript as an object, a Vector3 as a Vector3.
    Events.emit('example:welcome', { name = who.name, count = greeted, spawn = spawn })
end)

-- The timers of JavaScript, with the delay in milliseconds.
local beats = 0
local heartbeat
heartbeat = setInterval(function()
    beats = beats + 1
    if beats == 3 then
        clearInterval(heartbeat)
        print('ready after ' .. beats .. ' beats')
        Events.emit('example:ready')
    end
end, 10)

Events.on('resourceStop', function(name)
    if name == 'lua-example' then
        print('greeted ' .. greeted .. ' resource(s)')
    end
end)
```

Both files are `code/tests/fixtures/resources/lua-example`, which the test
suite runs next to a JavaScript resource (`js-example`) that imports the
export and listens to the events.

### Script lists

- `serverScripts`, `clientScripts` and `sharedScripts` take `.lua` files as
  they take `.js` files. Scripts run in the order the manifest gives
  (shared scripts first, then the scripts of the side), whatever their
  language.
- A resource gets one Lua state per side when its script list for that side
  has a `.lua` file, and a JavaScript runtime when it has any other script. A
  Lua-only resource has no JavaScript runtime.
- `stop`, `restart`, `ensure` and the development file watcher treat a Lua
  state as they treat a JavaScript runtime: its handlers, timers, exports and
  subscriptions go with it. `errorBehavior` applies to Lua errors as it does
  to JavaScript ones.
- If a manifest names a `.lua` script and the host has no Lua engine, the
  resource fails to start with `Lua scripting is not available`.

### Mixed-language resources

A resource may list `.js` and `.lua` scripts together. The two languages of
one resource share nothing: no globals, no objects. They talk through events,
exports and messages, exactly as two resources do, and values are copied on
the way (see [Crossing between languages](#crossing-between-languages-and-resources)).
Both runtimes are started, stopped and restarted together.

A declared export may be registered from either language, once: a second
`Exports.register` of a name fails with
`Exports.register: export '<name>' is already registered` when Lua is
involved on either side.

### `require` and `files`

`require(name)` loads a Lua file of the resource:

- `require('lib.util')` reads `<resource>/lib/util.lua`, then
  `<resource>/lib/util/init.lua`.
- A name is made of letters, digits, `_`, `-` and the dots between its parts,
  and is at most 256 bytes long. It cannot name anything outside the
  resource; there is no `package` table and no search path.
- The module runs once per state, with its name as `...`. `require` returns
  what the module returned, or `true` when it returned nothing.
- A module runs to its end inside the `require` call, so it cannot wait
  (`sleep`, an asynchronous API) while it loads.
- A missing module raises
  `module '<name>' not found in resource '<resource>'`. A module required
  again while it is still loading raises
  `require: cyclic dependency on '<name>'`. A module that fails is not cached.

```lua
-- client.lua
order = order .. ',client,' .. require('lib.util').name
```

```lua
-- lib/util.lua
return { name = 'util' }
```

On the client, a module reached by `require` must be shipped. Scripts named
in `clientScripts` and `sharedScripts` always ship. For modules:

- If the manifest declares `files`, list the modules there (globs are
  allowed). They are shipped and not executed.
- If the manifest declares no `files`, the packager scans as it does for
  JavaScript: every `.lua` file under the directories of the client and shared
  scripts ships, except the files named in `serverScripts`. A server-only
  helper that sits in such a directory and is not listed in `serverScripts`
  ships too. Declare `files` to control this.

Files named in `serverScripts` never ship, and a client cannot `require`
them.

## The dialect

Lua 5.5 (PUC Lua 5.5.1): 64-bit integers, integer division, bitwise
operators, `goto`, to-be-closed variables. Source files are text only; a
UTF-8 byte order mark at the start of a file is skipped.

## The sandbox

A Lua state has no access to files, processes or native code.

Available libraries: base, `coroutine`, `table`, `string`, `utf8`, `math`, and
of `os` only `time`, `clock`, `date` and `difftime`.

| Removed or changed | What it does here | Why |
|---|---|---|
| `io`, `debug`, `package` | not present | file, process and interpreter access |
| the rest of `os` (`execute`, `getenv`, `remove`, ...) | not present | process and file access |
| `dofile`, `loadfile` | removed | file access; use `require` |
| `string.dump` | removed | no bytecode |
| `load` | accepts a string chunk only, always compiled as text; anything else raises `load: only a string chunk can be loaded` | bytecode can crash the interpreter |
| `require` | reads `.lua` files of the resource and nothing else; no C modules | containment |
| `collectgarbage` | answers `"count"` only; any other option raises `collectgarbage: only "count" is available` | the host owns collection |
| `print` | writes one line to the scripting log as `[resource] line` | there is no terminal |
| `setmetatable` | refuses a metatable with `__gc`: `setmetatable: __gc is not available` | a finalizer runs where the deadline cannot stop it |
| `pcall`, `xpcall`, `coroutine.create`, `coroutine.wrap`, `coroutine.resume`, `coroutine.close` | behave as Lua's, except that none lets a script run past the deadline of an entry | a script must not catch its own time limit |

The coroutine the host runs a script in is not the script's to handle:
`coroutine.resume` on it returns
`false, "cannot resume a coroutine the host started"` and `coroutine.close`
raises `cannot close a coroutine the host started`.

### Output

`print` and `console.log` / `info` / `warn` / `error` / `debug` write to the
same place. `console` logs at the level of its function. For both:

- a line is cut at 4096 bytes and ends with ` ...(truncated)`;
- control characters other than the tab become spaces;
- a resource writes at most 200 lines a second; past that the log says
  `[resource] output suppressed: more than 200 lines in a second`.

### Limits

| Limit | Server default | Client default |
|---|---|---|
| Memory of one Lua state | 512 MB | 128 MB |
| Time of one entry into the state | none | 5000 ms |
| Pending timers of one state | 10 000 | 10 000 |
| Entries nested in one another | 64 | 64 |

These are the defaults of the Framework's server and client; a game may set
others.

An *entry* is one stretch a coroutine runs without waiting: a script file
from its start to its first wait, a handler, a timer callback, the part of a
function between two `sleep` calls. Each entry has its own deadline.

When a limit is hit:

- **Memory.** The allocation fails and Lua raises `not enough memory`, which
  is an ordinary error: it fails the script, handler or timer that asked,
  unless the script catches it with `pcall`.
- **Time.** The entry is aborted with `script ran too long`. `pcall` and
  `xpcall` do not stop it: the error is raised again, so the entry ends. A
  state with a deadline runs Lua code at about half the speed of one without,
  which by default is every client state.
- **Timers.** One more `setTimeout` or `setInterval` raises
  `setTimeout: too many timers (the limit is 10000)`.
- **Nesting.** A `spawn` or a call into the script nested deeper fails with
  `too many nested calls into the script (the limit is 64)`.

What happens to the resource depends on where the error ends up. An error in
a script file fails the start of the resource. An error in a timer callback or
a spawned function nobody waits for is a runtime error of the resource, and
the manifest's `errorBehavior` (`stop` by default, `restart`, `continue`)
decides. An error in an event handler goes to whoever emitted the event (see
[Events](#events)).

## API conventions

The reference for names and signatures is the pair of generated definition
files, `types/lua/server.d.lua` and `types/lua/client.d.lua`
(see [Editor support](#editor-support)). The rules below hold for all of it.

### Same names as JavaScript

Every global, class, method and property has its JavaScript name:
`Events.emit`, `Exports.register`, `Vector3`, `entity.position`,
`ExecutionEnvironment.isServer`. Documentation written for one language
names the right function in the other.

### `:` for methods, `.` for statics and namespaces

```lua
local spawn = Vector3.new(10, 0, 5)     -- static: dot
local d = spawn:distance(position)      -- method: colon
Events.on('ping', handler)              -- namespace function: dot
```

A method called with `.` raises: `spawn.distance(position)` is

```
bad receiver for 'Vector3:distance' (Vector3 expected, got ...)
```

This holds for every method and property of every class
(`entity.state.get('job')` raises too; write `entity.state:get('job')`). A
namespace function called with `:` gets the namespace as its first argument,
which is almost never what it expects: `console:log('x')` logs the console
table too.

A method on a handle whose entity is gone (a `Player` who left) does not
raise. It does nothing, as in JavaScript.

### Constructors

A class is constructed with `Class.new(...)` or by calling the class:
`Vector3.new(1, 2, 3)` and `Vector3(1, 2, 3)` are the same. A class a script
is only ever handed (`StateBag`, `PendingConnection`) is not a global and has
no constructor.

### Properties

Properties are read and assigned as fields: `v.x = 4`, `entity.position`,
`player.ping`.

- Reading a name the class does not have gives `nil`.
- Assigning to a read-only property or to an unknown name raises
  (`attempt to set 'length' on Vector3`). JavaScript ignores such an
  assignment outside strict mode.
- Class tables and namespaces are read-only: `Chat.sendToAll = nil` raises
  `attempt to set 'sendToAll' on Chat`.

Value classes also have operators where they make sense, in addition to the
named methods: `+`, `-`, `*`, `/`, unary minus and `==` on the vectors, `*`
and `==` on `Quaternion`, `==` on `Color`. `tostring(v)` is `v:toString()`.

### `nil`, numbers, strings, arrays

- JavaScript `null` and `undefined` are both `nil`. An optional argument is
  left out or passed as `nil`.
- A Lua table cannot hold `nil`. Where a JavaScript option takes `null`
  inside an object (`attachTo: null`, `showTo: null` of a `TextLabel`), a
  Lua script passes `false` for that key. Where a function answers with an
  object that has a `null` property in JavaScript, the key is absent in Lua.
- A parameter typed `integer` accepts an integer or a float with an integral
  value. A parameter typed `number` accepts both. A parameter typed `string`
  accepts a string only; a number is not converted. A `boolean` parameter
  accepts a boolean only.
- An array is a sequence starting at 1. `list[1]` is what JavaScript calls
  `list[0]`. An error that names an element of a Lua list counts from 1.
- An empty table `{}` is taken as an empty list where a list is expected.

### Errors

An API failure is a raised string, with the same message JavaScript gets in
its `TypeError` or `Error`, and with the position of the calling script in
front, as Lua does for its own errors:

```
main.lua:12: Messages.handle: handler must be a function
```

Wrong arguments to a typed method are worded by Lua:
`bad argument #1 to 'Vector3:add' (Vector3 expected, got number)`,
`wrong number of arguments to 'Vector3:set' (...)`.

There are no error classes. Catch with `pcall`:

```lua
local ok, answer = pcall(Messages.request, target, messageType, payload)
```

## Asynchronous functions

Lua has no `await`. A function that returns a Promise in JavaScript
*suspends the calling coroutine* in Lua and returns the settled value. If the
Promise would reject, the call raises. These functions are marked `---@async`
in the definition files: `Events.emit`, `Events.emitTo`, `Events.emitLocal`,
`Messages.request`, and `sleep`. A function received from another resource
(an export, a callback) waits the same way when the function behind it does.

```lua
function ask()
    local p = {n = 1}
    local answer = Messages.request('provider', 'next', p)
    first = answer.n .. '|' .. tostring(p.seen)
    -- A request made from where the reply of another arrived.
    chained = Messages.request('provider', 'next', answer).n
end
```

While one coroutine waits, everything else goes on running: other handlers,
timers, other resources.

### `sleep`, `spawn`, timers

| Global | What it does |
|---|---|
| `sleep(milliseconds)` | Suspends the calling coroutine for a time. Lua only. |
| `spawn(callback, ...)` | Runs a function in a coroutine of its own, at once and as far as its first wait. What it raises is not raised in the caller. Lua only. |
| `setTimeout(callback, milliseconds, ...)` | Calls a function once after a delay, in a coroutine of its own. Returns an id. |
| `setInterval(callback, milliseconds, ...)` | Calls a function repeatedly, each time in a coroutine of its own. Returns an id. |
| `clearTimeout(id)`, `clearInterval(id)` | Cancel them. |

Every script file, event handler, message handler, export call and timer
callback runs in a coroutine the host started, so all of them may wait:

```lua
Exports.register('slow', function(n) sleep(20) return n * 2 end)
```

Timing:

- Timers fire when the host ticks the scripting module. A tick fires what was
  due when it started, in due order; a timer armed by a callback waits for the
  next tick, however short its delay.
- A wait for no time (`sleep(0)`, `setTimeout(fn, 0)`) continues at the next
  tick. On the server a loop around `sleep(0)` goes round once per server
  tick.
- An interval that fell behind fires once, not once per period it missed.
- Timers and waiting coroutines end with the state. Nothing is called after
  the resource stopped.

### Where a script cannot wait

- At the top level of a module while `require` loads it.
- Inside a coroutine the script created itself (`coroutine.create`,
  `coroutine.wrap`).
- Where Lua cannot yield: inside a comparison function of `table.sort`, in a
  metamethod such as `__tostring`.

There the call raises `<function>: cannot wait here`, for example
`sleep: cannot wait here`, or
`sleep: cannot wait here: called inside a coroutine the script created`. To
wait from such a place, move the work into `spawn(function() ... end)`.

An asynchronous API only has to wait when its result is not known yet. An
`Events.emit` whose handlers all finish at once returns at once, anywhere. If
it does have to wait where it cannot, it raises `cannot wait here` after the
handlers were called; the same goes for `Messages.request`, whose handler ran
and whose reply is then lost.

`coroutine.yield()` in a script that made no coroutine of its own ends the
entry with `attempt to yield from outside a coroutine`: to the script, the
coroutine the host started is the main thread.

## Events

`Events` works as in JavaScript: `on`, `once`, `off`, `emit`, `emitTo`,
`onLocal`, `emitLocal`, `listenerCount`; on the server also `onClient`,
`onceClient`, `offClient`, `emitAllClients`; on the client `emitServer`.
`Events.on` returns a function that removes the handler.

```lua
Events.on('ping', function(n, s, flag, t, v)
    got = table.concat({tostring(n), s, tostring(flag), t.k, tostring(t.list[2]), v:toString()}, '|')
    t.k = 'changed'
end)
```

```lua
function fire()
    local t = {k = 'y', list = {1, 2}}
    local results = table.pack(Events.emit('ping', 1, 'x', true, t, Vector3.new(1, 2, 3)))
    return t.k .. '|' .. results.n
end
```

After `fire()` the handler above holds `1|x|true|y|2|Vector3(1, 2, 3)` and
the sender's `t.k` is still `'y'`:

- A Lua handler gets **copies** of the arguments, and a Lua emit copies its
  arguments once, whoever listens. Changing a received table changes nothing
  for the sender. (Two JavaScript handlers in one runtime still share the
  emitter's objects, as before.)
- An argument that cannot be copied makes the emit raise
  `Events.emit: argument N: <why>` before any handler runs.
- `Events.emit` waits until every handler of either language has finished,
  those that wait included, and returns `nil`.
- If handlers failed, the emit raises one string:
  `One or more event handlers failed: <error 1>; <error 2>`. A handler's error
  belongs to the emitter; it is logged, and it is not a runtime error of the
  handler's resource.
- A function among the arguments arrives as a function that calls the
  original.

The lifecycle events are events like any other, and their handlers may wait:

```lua
Events.on('resourceStop', function(name) if name == 'bank' then sleep(5) end end)
```

## Exports and imports

```lua
Exports.register('describe', function(item) return { label = item.name .. ' x' .. #item.sizes, origin = Vector3.new(1, 2, 3) } end)
Exports.register('slow', function(n) sleep(20) return n * 2 end)
```

The names are declared in the manifest (`"exports":["describe","slow"]`). An
undeclared name logs a warning and is not stored, as in JavaScript. A
JavaScript resource imports them like any other export:

```js
const provider = Imports.get('provider');
const described = provider.describe({ name: 'coat', sizes: [1, 2, 3] });
globalThis.described = described.label + '|' + (described.origin instanceof Vector3) + '|' + described.origin.y;
globalThis.slow = 'pending';
provider.slow(21).then((value) => { globalThis.slow = 'resolved ' + value; }, (error) => { globalThis.slow = 'rejected ' + error.message; });
```

And from Lua:

```lua
add = Exports.get('provider', 'add') api = Exports.get('provider', 'api')
```

```lua
Imports.get('provider').add(4, 4)
```

- What a Lua script registers is a **snapshot** taken at `register`: a table
  changed afterwards is not seen by importers. Share state through exported
  functions.
- What a Lua script imports is a copy; functions in it call the owner.
- A value that cannot cross raises at registration:
  `Exports.register: value: <why>`.
- An exported function that finishes at once gives its caller the value. One
  that waits makes a Lua caller wait and hands a JavaScript caller a Promise.
- Calling an export of a resource that has stopped raises
  (`runtime stopped`). `Imports.get` of a resource that is not running raises
  `Imports.get: resource '<name>' is not running`.

## Messages

```lua
Messages.handle('next', function(payload, reply)
    payload.seen = true
    kind = type(reply)
    reply({n = payload.n + 1})
end)
```

`Messages.request(resourceName, messageType, payload)` waits for the reply
and returns it (the `ask()` example above). `Messages.send` is a notification
and returns nothing.

- When the requester or the handler is Lua, payload and reply are copies: the
  handler above sets `payload.seen` on its own copy.
- A handler may reply later, after a `sleep`. Only the first reply counts; a
  later or second one does nothing.
- A request raises at once for `Target resource not found`,
  `No handler for message type`, and a payload that cannot be copied
  (`Messages.request: payload: <why>`). It raises what the handler failed
  with when the handler fails before replying, and
  `Messages.request: resource '<name>' stopped before reply` when the
  handler's resource stops first.
- There is no timeout: a request whose handler never replies waits until that
  resource stops.
- A handler failure nobody waits for (a notification, or a failure after the
  reply) is a runtime error of the handler's resource when Lua is involved.

## Crossing between languages and resources

A value crosses a boundary whenever Lua is on either end of an event, an
export or a message, and between any two resources. It is copied; nothing is
shared.

| JavaScript value | From Lua | Into Lua |
|---|---|---|
| `undefined`, `null` | `nil` produces `undefined` | `nil` |
| boolean | boolean | boolean |
| number | float; integer within ±2^53 | an integral value within ±2^53 arrives as an integer, anything else as a float |
| BigInt | integer beyond ±2^53 | integer when it fits 64 bits, refused otherwise |
| string | valid UTF-8 string; other strings are refused | string |
| Array | table whose keys are exactly `1..n`; **an empty table is an empty Array** | sequence; a `null` element is a hole |
| Object | any other table with string or integer keys (integer keys become their decimal text); other key types are refused | table with string keys |
| Date | — | integer milliseconds since the epoch |
| Map, Set | — | table (string/number keys only) / sequence |
| ArrayBuffer, typed array | — | string of the bytes |
| Error | — | table `{ name, message, stack }` |
| value type or handle (`Vector3`, `Color`, `Player`, ...) | the same class | the same class |
| function | function | callable |
| RegExp | — | refused |

Refused from Lua: cycles, coroutines, userdata that is not a Framework
class. Metatables are dropped, and no metamethod runs while a table is read.

Things to know:

- One value crosses per call result: a Lua function's first return value.
- A `null` or `undefined` property of a JavaScript object is absent in the
  Lua table.
- Object keys arrive as strings. A JavaScript key `"10"` stays the string
  `"10"`.
- NaN, the infinities and negative zero stay floats.
- A copy is bounded: 128 levels of nesting, about a million values, 256 MB of
  strings and binary data.
- A refusal says where in the value it happened, counting from 1 in a value
  read from Lua: `Cannot copy value.items[2].job: a coroutine cannot be
  copied`.
- A class a game bound for JavaScript only is refused with
  `host type '<Name>' is not available in Lua`.

Network events and state bags travel as JSON, as before. From Lua the JSON is
written as `JSON.stringify` writes the same data, with the mapping above: an
empty table is `[]`, NaN and the infinities are `null`. A function, a cycle
and a string that is not UTF-8 are refused. Read back, `null` is `nil`.

### Functions

A function that crosses becomes a callable in the receiving runtime. Calling
it runs the original in its own resource, with arguments and result copied.

- It can be kept and called later, until its resource stops. After that a
  call raises (`runtime stopped`).
- A function that crosses twice arrives as the same function (`rawequal`).
  Sent back to its owner, it arrives as the original.
- A JavaScript function that returns a Promise makes a Lua caller wait. A Lua
  function that waits hands a JavaScript caller a Promise.

### `api:method()` and the holder rule

A function that arrived as a member of a table can be called with `.` or
with `:` on that table. Both are the same call:

```lua
api.deposit(5) + api:deposit(5)
```

```lua
Imports.get('provider'):add(4, 4)
```

The function drops a first argument that is the very table it arrived in.
With any other first argument it passes everything on.

The function behind it is never called *on* that object:

- A JavaScript method called from Lua has the global object of its own
  resource as `this`, not the exported object. A JavaScript export whose
  methods use `this` does not work from Lua.
- A Lua export written as `function bank:deposit(n)` gets no `self` from an
  importer of either language: its first parameter is the importer's first
  argument.

Write exported functions over a closure (`function(n) ... end`, an arrow
function). `docs/scripting_resource_isolation.md` has the full rule.

## Client specifics

- **Limits.** 128 MB a state and 5000 ms an entry by default. A client script
  that loops for five seconds is aborted with `script ran too long`.
- **What ships.** Client and shared scripts, and what `files` lists or the
  scan finds (see [`require` and `files`](#require-and-files)). `require`
  reads the downloaded package, not the disk.
- **Shared builtins.** The value types, `Events` (with `emitServer`),
  `Messages`, `Exports`, `Imports`, `console`, `ExecutionEnvironment`,
  `Entity`, `Player`, `TextLabel` and `StateBag`, with the members of the
  client side. Server-only members are `nil` on the client
  (`Events.emitAllClients`, `player.kick`, `entity.state.set`).
- **Client-only builtins.** `Web`, `Key`, `Chat` (the client one: `send`,
  `open`, `close`, ...), `Discord`, `Voice` (the client one: `setEnabled`,
  `setVolume`, ...) and `Nametags`. `types/lua/client.d.lua` lists them.
- A game adds its own builtins on top; those are documented by the game.

The server has `Chat` (`sendToAll`, `sendToPlayer`), `Voice` (ranges, mutes),
`PendingConnection` and the server members of `Events`, `StateBag`, `Entity`,
`Player` and `TextLabel`.

## Editor support

`types/lua/server.d.lua` and `types/lua/client.d.lua` are definition files
(`---@meta`) for the Lua language server, LuaLS. They are generated from the
same declarations the bindings are built from. They describe every global,
class, member, operator, and which functions wait (`---@async`).

Point LuaLS at the file of the side you are writing for, in the `.luarc.json`
of the resource:

```json
{
  "workspace.library": ["/path/to/Framework/types/lua/server.d.lua"]
}
```

Use one file per workspace folder: both declare `Chat` and `Voice`, with
different members. LuaLS does not know the sandbox: it will still offer `io`,
`debug` and `package`, which a resource does not have.

These files have not been validated by running LuaLS. They use only
annotations that are valid for Lua 5.4; whether your LuaLS version knows the
5.5 dialect is for you to check.

What the files cannot say: the events the Framework raises and what their
handlers receive, the fields of an option table where a parameter is typed
`table`, and the comparison operators of a class (the class description
names them).

## Known limitations

- **C library loops are not interrupted.** The time limit is checked between
  Lua instructions, not inside a library function written in C. A
  pathological `string.find` pattern or a `table.move` over a huge range can
  still stall a client.
- **No `__gc`.** Script-defined finalizers are not available. Use
  to-be-closed variables (`local x <close>`) or the `resourceStop` event.
- **The deadline halves VM speed** in a state that has one, which by default
  is every client state.
- **Addresses are visible.** `string.format('%p', v)` and `tostring` of a
  table, a function or a userdata print addresses of the process.
- **Large unsigned ids.** A 64-bit unsigned id above 2^63 reads as a negative
  integer.
- **No timeouts on `Messages.request`.**
- **Exports from Lua are snapshots**, and exported methods get no `self`
  (see above).
- **`null` inside option tables** is written `false` in Lua, and a `null`
  property of a returned object is an absent key. So passing
  `label:getOptions()` to another label's `update` leaves that label attached
  where the same code in JavaScript would detach it.
- **A truncated line may end inside a UTF-8 character.**
- **No C modules, no LuaRocks, no bytecode.**
- **Editor definitions are unvalidated** against LuaLS (see above).
- **Game-specific bindings** written against raw V8 are not available in Lua
  until the game ports them.
