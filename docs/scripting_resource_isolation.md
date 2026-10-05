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
  its `resourceStop` handlers settle.

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

A function reference calls back into the resource that owns the function, with
its arguments and its result copied the same way. A reference whose owner has
stopped throws when called.

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

### Breaking changes for projects (C++)

- `Engine::GetIsolate()` and `GetContext()` stop naming "the" scripting
  isolate. Code that needs a resource's isolate asks for that resource's
  runtime.
- Native events are emitted with an argument builder that is called once per
  runtime, inside that runtime, instead of with a ready-made argument vector.
  Projects that already build their arguments in a callback (kcd2's
  `EmitReservedEvent`) change one call.
- The SDK register callback runs once per runtime rather than once per server.

This is a scripting-layer change and a minor version bump under the versioning
rules.

## Plan

1. **Runtime split** - `NodeRuntime` extracted from `NodeEngine`; the engine
   can create further runtimes. No behaviour change. *(done)*
2. **Value transfer** - copy a value out of one isolate and into another, with
   the table above; unit-tested across two runtimes.
3. **Function references** - call a function owned by another runtime,
   synchronously, with arguments and results transferred; promises settle
   across runtimes.
4. **Per-resource runtimes** - `ResourceManager` creates and destroys a runtime
   with each resource; builtins and the SDK callback register per runtime;
   every runtime is ticked; uncaught errors are attributed by runtime instead
   of by stack.
5. **Cross-runtime builtins** - events, exports, messages and state bags
   dispatch across runtimes through the transfer and references above.
6. **Remove the shims** - timer ownership, module eviction and stack-based
   resource lookup go from the Node engine.
7. **Projects** - kcd2, m2o, m3o and hogwarts move to per-runtime emission and
   registration.
8. **Follow-ups** - a per-resource monitor (time and heap per runtime, slow
   handler warnings), an opt-in for worker threads per resource, and inspector
   access to a chosen resource.

## Open questions

- **Inspector.** Only one environment can own it. Debugging a resource needs
  either the inspector attached to that resource's environment on demand, or
  Node's worker-style inspector parent handles.
- **Cost per runtime.** Each environment pays for an isolate and Node's
  bootstrap. To be measured on a server with many resources before this lands.
- **Worker permission.** FiveM refuses workers unless the operator names the
  resource. Whether to do the same is a security decision; adding it later
  breaks servers that already rely on workers.
