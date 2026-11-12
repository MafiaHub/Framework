# Resource Isolation on the Server

Every server resource runs in its own Node.js environment: its own isolate,
libuv loop, context and `require` cache. This is the model FiveM's server
uses (`citizen-scripting-node`), and this document is the plan for moving the
framework's server scripting to it.

The client is out of scope. It runs on the standalone V8 engine with one
shared isolate and keeps doing so.

## Why

Today every server resource shares one Node environment. Stopping a resource
has to find and undo everything it created, one kind of thing at a time:

- event handlers, message handlers and state bag subscriptions are dropped by
  name;
- timers are found through a `setTimeout`/`setInterval` shim that records the
  owner of each callback;
- the `require` cache is evicted by path, and ESM imports get a new URL
  identity, so a restart re-reads edited files.

Anything without such a shim outlives its resource. A tester running a large
roleplay server found the first one: worker threads started with
`node:worker_threads` keep running after `stop` or `ensure`, so every reload
leaks a thread. Open sockets, HTTP servers and child processes are the same
class of leak.

With one environment per resource, stopping the resource frees its
environment. `node::FreeEnvironment` stops and joins the environment's
workers and closes its handles, so a resource cannot leak what it started, and
the shims above are no longer needed. Resource environments are created with
`kTrackUnmanagedFds`, so files opened with `fs.open()` are closed with them too.

It also gives every resource a heap of its own, which makes per-resource
memory figures and limits possible, and lets permissions such as worker
spawning be granted per resource.

It does **not** stop one resource from blocking the others. All environments
run on the server's scripting thread, as FiveM's do. Long work still belongs in
a worker thread; what changes is that the worker now dies with its resource.

## The model

- `NodeRuntime` is one environment. The engine initialises the process-wide
  platform once; every runtime is created against it and run on the
  scripting thread.
- A runtime runs only when it has work, in a tick or between two: I/O on its
  loop, a due timer or immediate, or a call into it whose queues wait for the
  tick. A resource with nothing due costs nothing, whatever it holds, so the
  idle cost of a server does not grow with its resources. Between ticks the
  server runs a runtime as soon as it has work (`NodeEngine::RunUntil`), and
  starts nothing in the last millisecond before the tick.
- Each runtime keeps a referenced handle of its own on its loop, as Electron
  does for the loop it embeds, so the loop is always alive and `uv_run` runs
  whatever reaches it. That includes the platform's tasks: V8 posts them to
  the isolate's loop (`uv_async_send`), and its delayed ones become
  unreferenced timers there, which never ran on a loop nothing else kept
  alive (an `Atomics.waitAsync` timeout, for one).
- How the server learns a loop has I/O depends on the platform. On Linux and
  macOS it polls every loop's backend fd at once, libuv's own embedding API.
  A Windows loop is a completion port, which libuv offers no way to wait on
  from outside, so each runtime has a watcher thread blocked on its port, as
  Electron's `NodeBindingsWin` does. The watcher takes a completion, gives it
  back unchanged (libuv reads a completion from its `OVERLAPPED`), signals
  the server and parks until the loop has run. It signals and parks in one
  step (`SignalObjectAndWait`): libuv creates the port for one running
  thread, and a watcher still running when the loop dequeues would hide the
  packet from it (electron/electron#14319).
- The tick's own work goes to the runtimes with work too. V8 reports every
  outermost call into an isolate as it completes
  (`AddCallCompletedCallback`), so a runtime entered since its last tick, by
  any path, is ticked to run what that call queued. Script that runs in a
  tick marks the runtime again, so it gets one more tick for what the loop
  does not show, such as an unreferenced immediate.
- Finding the runtimes with work costs nothing per idle runtime either. A
  busy loop wakes the server thousands of times a second, and asking every
  loop for its next timer on each wake-up cost more than the loop's own work
  (beside 200 idle resources, a third of the round trips were left). Runtimes
  report to a `RuntimeSchedule` instead: their watcher names the loop with a
  completion, and a runtime's due time is read from its loop again only after
  something ran in it or entered it, which is all that can change it. On
  Linux and macOS each wait still polls every loop's fd.
- The engine's first environment is created with the default flags and owns
  the inspector and the process state (working directory, title, signals).
  Every further environment is created with `kNoCreateInspector`: Node allows
  one inspector agent per process and asserts on a second. A sandboxed engine
  applies its sandbox to every resource's environment, not only its own.
- A resource's runtime is created when the resource starts and destroyed after
  its `resourceStop` handlers settle. A runtime stopped from inside itself (a
  resource stopping itself, one it is waiting on, or one stopped by another
  resource's event handler) is destroyed at the next tick instead, once no
  script is running at all: native code further up the stack may still hold
  handles into it. A Lua script on the stack counts as much as JavaScript does,
  since it reaches JavaScript runtimes through the binding layer; and a Lua
  runtime stopped while JavaScript is on the stack waits for the next tick of
  the Lua engine the same way (`ScriptActivity`, `scripting/script_engine.h`).
- `process.exit()` stops the resource that calls it, not the server. Node's
  default exit handler would end the process; each resource's environment gets
  one that stops its JavaScript at once. From the next tick the runtime is cut
  off (calls into it throw, what it owed other resources is rejected, its
  handlers are dropped) and the resource is stopped, once any start or stop
  already under way has finished. A resource that exits while it loads fails to
  start. Node's `process.on('exit')` handlers run only when the resource calls
  `process.exit()` itself, not when it is stopped; use `resourceStop`.
- The engine keeps its own runtime too, the host. Native code that runs outside
  any resource builds its values there, and they are copied into each resource
  that receives them.
- `Engine::GetIsolate()` and `GetContext()` name the runtime the thread is
  executing in: a resource's own while inside it, the host otherwise. Native
  code reached from a resource (a binding, an event it emits) therefore works
  in that resource's isolate without being told which one it is.
- A call into a runtime from outside its JavaScript - the host entering through
  `EngineScope` or `Engine::Execute`, another runtime calling an export or a
  handler, a reply or a cross-runtime promise settling - runs inside a Node
  callback scope, as Node's own callbacks do. What it queues with
  `process.nextTick` or a promise runs as the call returns, before the host
  does anything else, and an unhandled rejection or a throwing `nextTick`
  callback reaches the resource's error handling then. Entries that nest (a
  handler emitting to its own runtime, a builtin entering the engine from
  inside a script) drain once, when the outermost returns. A call made through
  raw V8 with no scope has what it queued run at the runtime's next tick
  instead.
- Each runtime gets the same bindings: the engine runs the setup callback
  inside every runtime it creates, and the server module registers the
  framework builtins and the project's SDK callback from it.
- An uncaught error in a runtime belongs to its resource. It is attributed
  directly, not by reading file paths out of the stack.

### What crosses between resources

Values no longer pass between resources by reference. Anything that crosses
(event arguments, export values and calls, message payloads, handler results)
is copied out of one isolate and rebuilt in the other:

| Value | Arrives as |
| --- | --- |
| `undefined`, `null`, booleans, numbers, bigints, strings | the same value |
| arrays, plain objects | copies, with cycles rejected |
| `Date`, `Map`, `Set`, `ArrayBuffer`, typed arrays | copies |
| framework value types (`Vector3`, `Quaternion`, `Color`, ...) | the same type, by value |
| framework entities (`Player`, ...) | the same entity, wrapped again |
| functions | a callable reference into the owning resource |
| instances of a resource's own classes | plain objects; the prototype does not cross |
| symbols, promises, cycles, weak collections | refused, with the path to the value |
| more than about a million values, or 256 MB of strings and binary data | refused |
| `SharedArrayBuffer` and views on one | refused (see below) |

An object reached twice is copied twice; the bound above is what stops a few
nested references from expanding into a copy without end.

A function reference calls back into the resource that owns the function, with
its arguments and its result copied the same way. A reference whose owner has
stopped throws when called. A resource that receives the same function twice
gets the same reference both times. A function sent as a property of an object
(an exported API's method) runs with the owner's object as `this`, so methods
work across resources and act on the owner's state, not on the caller's copy.

Properties are rebuilt as own data properties: an own `"__proto__"` key arrives
as an own property and never replaces the prototype.

An event or message handler in another resource is called for its outcome
only: what it returns is never copied back, so returning an object that cannot
cross (a timer, a socket) is not an error. A returned `false` still vetoes a
cancellable event, a returned promise is still waited on, and a throw or
rejection still reaches the emitter.

The same holds for an event that involves Lua, on either end and also inside
one resource. A Lua handler receives copies and is called for its outcome
only: `false` returned before it first waits vetoes, the emitter waits until
its coroutine ends, and what it raises reaches the emitter. `Events.emit` from
Lua copies its arguments once, waits until every handler of either language
has settled, returns nothing, and raises `One or more event handlers failed`
followed by what each handler failed with. A handler whose resource stops
while an emit waits on it fails that emit with
`<resource> stopped before the call finished`.

Messages cross languages the same way. `Messages.request` from Lua waits
where JavaScript gets a Promise: it returns the reply, or raises what the
Promise would be rejected with (`Target resource not found`,
`No handler for message type`, what the handler failed with,
`Messages.request: resource '<name>' stopped before reply`). A reply is
delivered at the next update of the server, also when the handler answered at
once, so a request needs a place where Lua can wait. A handler of either
language may answer later, a Lua one after a `sleep`; only the first reply
counts, and a reply to a request that is gone does nothing. Where the
requester or the handler is Lua the payload and the reply are copies, and a
Lua caller whose payload cannot be copied is told as it calls
(`Messages.request: payload: ...`). A request nobody answers waits until the
resource that owes the answer, or the one that asked, stops, in both
languages. A handler that fails while its request still waits fails that
request; a failure nobody waits for (a notification, a handler that fails
after it replied) is reported against the handler's resource when Lua is
involved, and only logged between two JavaScript resources, as before.

Exports cross languages the same way. What a Lua script registers with
`Exports.register` is copied when it is registered, its functions as
functions that call home, and every importer of either language gets a
rebuild of that copy: a table changed after it was registered is not seen,
so share state through exported functions. A Lua importer gets a copy of a
JavaScript export, made when it asks. Functions in it are called as the next
section says. Only an export that goes from JavaScript to JavaScript keeps the
owner's object as `this`. A call
returns the value itself when the function finished at once; otherwise a
JavaScript caller gets a Promise and a Lua caller waits, and a rejection or
an error raised late is thrown to it. A name is registered once per start of
a resource where Lua is involved: a second `Exports.register` of it throws
`Exports.register: export '<name>' is already registered`. JavaScript
registering over its own export replaces it, as before.

#### Calling a function that arrived in a table, from Lua

Every table a Lua script receives from another runtime is a copy the host
built: an event argument, the result of a call, an export, and the table
`Imports.get` returns. A function that arrived as a member of such a table
can be called with `.` or with `:` on it. `api:deposit(5)` and
`api.deposit(5)` are the same call, and so are `Imports.get('bank'):add(1, 2)`
and `Imports.get('bank').add(1, 2)`: the function drops a first argument that
is the very table it arrived in, however the call was written.

- With any other first argument the function passes every argument on. So
  does a function taken out of its table and put in another one, and a
  function that arrived on its own.
- A script that really wants to hand a function the table it came in, as its
  first argument, must pass a copy of the table or pass it in another
  position: `api.inspect(api)` calls `inspect` with no argument.
- The function behind it is called on nothing in every case. A JavaScript
  function called from Lua has `this` undefined, so a JavaScript export whose
  methods use `this` does not work from Lua. A Lua export written as
  `function bank:deposit(n)` gets no `self` from an importer of either
  language: its first parameter is the importer's first argument. Write
  exported functions over a closure (`function(n) ... end`, an arrow
  function) instead.
- A resource that imports its own Lua export gets its own functions back,
  which are called exactly as the script wrote the call.

Shared memory cannot be shared between resources. Each environment frees its
array buffers through its own allocator, which dies with the resource, so a
`SharedArrayBuffer` that outlived the resource that made it would crash the
server when its last holder let go. Sharing it with the resource's own worker
threads is unaffected.

### Breaking changes for resources

- An object shared through an export is a copy. Changing it in one resource is
  not seen by the other; share state through exported functions instead.
- Instances of a resource's own classes lose their prototype when they cross.
- `instanceof` against another resource's classes is false, including the
  built-ins (`Error`, `Array`) of another environment.
- `globalThis` is no longer shared. Anything one resource put there for
  another to read is gone.
- Only the first environment owns process state, so calls such as
  `process.chdir()` throw `ERR_WORKER_UNSUPPORTED_OPERATION` inside a
  resource.
- `process.exit()` inside a resource stops that resource instead of ending
  the server process.

Resources that pass plain data through events and exports, and call exported
functions with it, are not affected.

### Changes for projects (C++)

Most project code keeps working, because `GetIsolate()` follows the runtime
that is executing and values built in the host are copied into resources:

- `ModuleRegister` runs once per runtime, not once per server, and registers
  into whichever runtime is being set up.
- Native events keep their shape. Arguments built in the host are copied into
  each resource that handles them, so a project that builds them per isolate
  (kcd2's `EmitReservedEvent`) needs no change.
- A project's own handle types (kcd2's `Horse`, `Npc`, ...) must be registered
  with `Builtins::RegisterHandleTransfer<T>(name)` in `PostInit`, each derived
  class after its base; anything else with `ValueTransfer::RegisterHostType`.
  Unregistered, a handle derived from `Player` or `Entity` arrives as the
  framework base class. The framework's value types and `Entity`, `Player`,
  `TextLabel` and `StateBag` are registered already.
- Per-isolate state must really be per isolate. A class cache held in a single
  static (one `v8pp::class_<T>` for the whole server) breaks the first time a
  resource's runtime registers bindings; key it by isolate.
- A `v8::Global` a project keeps beyond one call is bound to the runtime it was
  made in, which may now be a resource's that is destroyed when the resource
  stops. Drop such handles from `Engine::AddRuntimeDisposingListener`, which
  runs with the isolate still alive, and resolve a pending promise inside the
  isolate it was made in, not the one `GetIsolate()` names at that moment.
- Enter a runtime through `EngineScope`, or run a script through
  `Engine::Execute`, so that what the call queues runs as it returns. A call
  made with only V8's own scopes (`v8::Locker`, `Isolate::Scope`,
  `Context::Scope` and a `Function::Call`) has no Node callback scope: its
  `process.nextTick` callbacks and promise continuations wait for the
  runtime's next tick, or for the next scoped call into it to return.

This is a scripting-layer change and a minor version bump under the versioning
rules.

### Monitoring resources

`ResourceMonitor` (`scripting/resource_monitor.h`) charges script time to the
runtime the engine enters: its own tick and its loop runs between ticks
(timers, I/O, `await` continuations), a cross-runtime call or settlement, an
event or message handler, a state bag listener, a message reply. What an entry
queues runs as it returns, and is charged with it. Entering a runtime pauses
the one it was entered from, so a resource is not charged for the resources it
calls. A resource re-entered through another (its handler calls a second
resource, whose code raises an event the first one handles) is charged for
both of its frames, as its own time. The server closes one frame at the top of
each server tick (`NodeEngine::CloseMonitorTick`), so script run between two
ticks counts in the first one's frame. Once a second the monitor also reads
each runtime's heap and its collector's pauses, counted from zero at the
runtime's start, so a resource's startup collections are in its first second.
Its window is the last minute of wall time, however long the ticks in it took.
It runs on the main thread only.

- `NodeEngine::GetRuntimeStats()` returns, per runtime, the mean and worst
  script time per server tick over the last minute, total script time, heap
  used, committed and limit, external memory, collector time per second and
  pause count, and the slow reports below.
- The `resmon` console command prints the time view of them: per runtime, the
  mean and worst script time per tick, heap used and limit, collector time per
  second and slow reports, sorted by script time. `resmon heap` prints the
  memory view: heap used, committed and limit, external memory, collector time
  and pauses, sorted by what each runtime holds.
- An event or message handler, state bag listener, export call, message reply,
  runtime tick or loop run between ticks whose own script time reaches
  `slow-script-ms` (server.json, default 50, 0 turns it off) logs one line
  naming the resource and, for a handler or listener, the event or key, for an
  export call, the function's name and the calling resource; at most once every
  10 s for the same resource, kind and name. Time it spent in other resources
  is theirs and is reported against them.
- With Tracy connected, each runtime's tick is a `Scripting::Runtime` zone
  and each loop run between ticks a `Scripting::RunLoop` zone, both named after
  its resource, and every collector pause of 1 ms or more is a
  message naming the runtime.

## Plan

1. **Runtime split** - `NodeRuntime` extracted from `NodeEngine`; the engine
   can create further runtimes. No behaviour change. *(done)*
2. **Value transfer** - copy a value out of one isolate and into another, with
   the table above; unit-tested across two runtimes. *(done)*
3. **Function references** - call a function owned by another runtime,
   synchronously, with arguments and results transferred; promises settle
   across runtimes. *(done)*
4. **Per-resource runtimes** - `ResourceManager` creates and destroys a runtime
   with each resource; builtins and the SDK callback register per runtime;
   every runtime is ticked; uncaught errors are attributed by runtime instead
   of by stack. *(done)*
5. **Cross-runtime builtins** - events, exports, imports and messages dispatch
   across runtimes through the transfer and references above; state bag
   subscriptions are per isolate already and go with the runtime. *(done)*
6. **Remove the shims** - the timer-ownership wrapper, module eviction and the
   ESM reload hooks go from the Node engine; a fresh runtime per start does
   their work. The client's V8 engine keeps its own. *(done)*
7. **Projects** - kcd2, m2o, m3o and hogwarts register their handle types and
   drop the handles they keep in a resource's runtime when it stops.
8. **Follow-ups** - a per-resource monitor (time and heap per runtime, slow
   handler warnings) *(done, see "Monitoring resources")*, an opt-in for worker
   threads per resource, and inspector access to a chosen resource.

## Open questions

- **Inspector.** Only one environment can own it. Debugging a resource needs
  either the inspector attached to that resource's environment on demand, or
  Node's worker-style inspector parent handles.
- **Cost per runtime.** Each environment pays for an isolate and Node's
  bootstrap. Measured on Apple Silicon (release build, 30 trivial resources):
  about 15 ms to start a resource, 6.4 MB of resident memory each, and 0.2 ms
  to stop one. Idle, a resource costs nothing: on Windows (RelWithDebInfo),
  60 resources holding a timer or nothing add under 3 ms of scripting-thread
  time a second, where ticking every runtime and visiting every live loop each
  millisecond cost about 90 ms. On Windows each runtime also has a watcher
  thread, blocked on its loop's port while there is no I/O. A server with 200
  resources would spend about 3 s starting them and 1.3 GB holding them. Starting from a
  Node snapshot could cut the bootstrap: node.h has the embedder API for it
  (`CommonEnvironmentSetup::CreateFromSnapshot`, `EmbedderSnapshotData`),
  marked experimental, and it is not measured yet. Still to measure on Windows
  and Linux servers.
- **Worker permission.** FiveM refuses workers unless the operator names the
  resource. Whether to do the same is a security decision; adding it later
  breaks servers that already rely on workers.
