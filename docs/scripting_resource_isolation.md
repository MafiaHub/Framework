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
the shims above are no longer needed.

It also gives every resource a heap of its own, which makes per-resource
memory figures and limits possible, and lets permissions such as worker
spawning be granted per resource.

It does **not** stop one resource from blocking the others. All environments
run on the server's scripting thread, as FiveM's do. Long work still belongs in
a worker thread; what changes is that the worker now dies with its resource.

## The model

- `NodeRuntime` is one environment. The engine initialises the process-wide
  platform once; every runtime is created against it and ticked on the
  scripting thread.
- The engine's first environment is created with the default flags and owns
  the inspector and the process state (working directory, title, signals).
  Every further environment is created with `kNoCreateInspector`: Node allows
  one inspector agent per process and asserts on a second.
- A resource's runtime is created when the resource starts and destroyed after
  its `resourceStop` handlers settle. A runtime stopped from inside itself (a
  resource stopping itself, or one it is waiting on) is destroyed at the next
  tick instead, once nothing is executing in it.
- The engine keeps its own runtime too, the host. Native code that runs outside
  any resource builds its values there, and they are copied into each resource
  that receives them.
- `Engine::GetIsolate()` and `GetContext()` name the runtime the thread is
  executing in: a resource's own while inside it, the host otherwise. Native
  code reached from a resource (a binding, an event it emits) therefore works
  in that resource's isolate without being told which one it is.
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
| `SharedArrayBuffer` and views on one | refused (see below) |

A function reference calls back into the resource that owns the function, with
its arguments and its result copied the same way. A reference whose owner has
stopped throws when called.

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
  with `ValueTransfer::RegisterHostType`, after the framework's, or they arrive
  in resources as plain objects. The framework's value types and `Entity`,
  `Player`, `TextLabel` and `StateBag` are registered already.
- A `v8::Global` a project keeps beyond one call is bound to the runtime it was
  made in, which may now be a resource's that is destroyed when the resource
  stops. Such handles must be dropped when the resource stops.

This is a scripting-layer change and a minor version bump under the versioning
rules.

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
   handler warnings), an opt-in for worker threads per resource, and inspector
   access to a chosen resource.

## Open questions

- **Inspector.** Only one environment can own it. Debugging a resource needs
  either the inspector attached to that resource's environment on demand, or
  Node's worker-style inspector parent handles.
- **Cost per runtime.** Each environment pays for an isolate and Node's
  bootstrap. Measured on Apple Silicon (release build, 30 trivial resources):
  about 15 ms to start a resource, 6.4 MB of resident memory each, 0.2 ms to
  stop one, and 0.015 ms to tick all 30 when idle. A server with 200 resources
  would spend about 3 s starting them and 1.3 GB holding them. Starting from a
  Node snapshot could cut the bootstrap; the embedder API to do it is not
  public yet. Still to measure on Windows and Linux servers.
- **Worker permission.** FiveM refuses workers unless the operator names the
  resource. Whether to do the same is a security decision; adding it later
  breaks servers that already rely on workers.
