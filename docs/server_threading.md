# Server threading

How a server uses the host's cores, and how game code takes part.

## The rule

There are two rules, one for entities and one for everything else.

**Entities are reached through handles.** A replicated entity, and mod state
registered beside the entities, is asked of `Replication::World`, which
answers with a handle that holds the entity's lock for as long as the handle
lives. This is the only way to an entity on a server, on every thread: the
tick, a script binding, a packet handler. There is no accessor that returns
a `NetworkEntity *`.

**Everything else belongs to one thread at a time.** What a mod's `Instance`
holds and has not registered is the tick thread's for the length of a tick
and the script thread's whenever it runs anything; the two hand it to each
other (`Utils::WorldLock`). The **packet thread** takes no part in that
handoff. Code that runs there touches entities and registered state by
handle, and anything else only if it guards it itself.

Every other thread either works on data it was handed, or runs while the
tick thread waits for it.

## The threads

| Thread | What it does | Sized by |
| --- | --- | --- |
| Tick | Interest, delegation, the replication pass, `PostUpdate`, completions | one |
| Packet | Everything that arrives: RPC handlers, inbound entity state, connections opening and closing | one |
| Script | Every resource's runtime, and what the other threads post to scripts | one |
| Network (MafiaNet) | Sockets, reliability, the voice relay | one |
| Replication workers | Viewer interest, construct/destroy lists, the serialize pass, a game's own per-tick loops | `replication-threads` |
| Background workers | Blocking work handed off the tick | `background-threads` |
| Script platform | V8 collection and compilation for every resource's isolate | cores |
| Web workers | Downloads, the info endpoint, mod routes | connection slots |

`Utils::ThreadBudget` shares the cores out. Left alone it gives replication
every core but three (tick, network and packet), up to 12, where the pass
stops being bound by its workers; one background worker per four cores, between 1 and 4;
and half the cores to the script platform, between 4 and 8.

`server.json` (or the flags of the same names) pins them:

```json
{
    "replication-threads": -1,
    "background-threads": -1
}
```

`-1` leaves the count to the machine. `0` keeps that work on the tick thread,
which is the setting for a one- or two-core host and for bisecting a problem.
The server logs the layout it chose at start.

## Loops inside a tick: `ForkJoinPool`

For work that has to be finished before the tick moves on and splits into
independent iterations:

```cpp
void MyServer::PostUpdate() {
    auto &npcs = _npcs;
    GetTickWorkers()->Run(static_cast<uint32_t>(npcs.size()), [&](uint32_t i, uint32_t lane) {
        npcs[i].path = FindPath(npcs[i], _scratch[lane]);
    });
}
```

`Run` returns when every iteration has run; the tick thread is one of the
lanes. Nothing else of the tick runs meanwhile, so iterations may read what
only the tick thread and the scripts touch, and write only their own result.
The packet thread does run meanwhile: an iteration that reads an entity
takes a handle on it, like any other code. `lane` is below `Lanes()` and
indexes per-thread scratch without a lock. A loop started from inside an
iteration runs on the thread that started it, under the same lane.

Replication uses these same workers, at a different point of the tick, so
the two never compete.

## Work that blocks: `TaskScheduler`

For work that may take longer than a tick (disk, a database, an HTTP call):

```cpp
Framework::CoreModules::GetTaskScheduler()->Compute(
    [path] {
        return LoadAccount(path); // a background worker: touch nothing the tick thread owns
    },
    [this, guid](Account account) {
        ApplyAccount(guid, std::move(account)); // the tick thread, at the top of a later tick
    });
```

- `RunInBackground(work)` and `RunInBackground(work, done)` run `work` on a
  background worker; `done` runs on the tick thread. Copy what `work` needs
  into it: the objects it was copied from may change or go before it runs.
- `PostToOwner(task)` runs `task` on the tick thread, from any thread.
- The tick drains completions once, before the replication pass. A task
  posted while it drains waits for the next tick.
- A task that throws is logged and dropped, and its completion is skipped.
- At shutdown, queued background work is finished; completions that no tick
  is left to run are dropped. A completion must not be the only place a
  resource is released.

The scheduler is Asio underneath: an `asio::thread_pool` for the workers and
an `asio::io_context` the tick thread polls. Asio stays out of the headers a
project includes.

The FTL job system (`jobs/`) is a different tool and is not used here. Its
tasks run on 512 KB fiber stacks, it drops queued tasks when it is destroyed,
and it switches the thread that owns it onto a fiber, which on a server is
the thread running V8.

## HTTP routes that read the world

A route registered with `RegisterRequest` runs on a web worker, several at
once. It may read only what it guards itself.

A route that reads or changes what the tick thread owns is registered with
`RegisterTickThreadRequest`. The web worker waits while the tick thread runs
the handler:

```cpp
GetWebserver()->RegisterTickThreadRequest("/players", [](const httplib::Request &, httplib::Response &res) {
    res.set_content(std::to_string(Framework::CoreModules::GetReplication()->GetConnectionCount()), "text/plain");
});
```

It answers 503 when the tick thread has not run it within five seconds, and
a handler given up on is never run afterwards; 500 when the handler throws.

## What the framework itself moved

- **Replication.** Interest is computed per viewer on the workers, and with
  it now what each viewer gains and loses against what it already holds, so
  the serial part of the pass only hands two lists to ReplicaManager3. A
  list is used only while nothing was destroyed since it was made; otherwise
  the pass decides again, as before. `PreparedDiffsLastPass()` counts them.
- **Publishing client files.** Packaging a resource, building its asset paks
  and hashing their chunk lists happen on a background worker when a
  resource is hot-reloaded, and on all replication workers at once at boot.
  New files are written beside the ones being served and take their place in
  a tick, so what the server has announced is always what it serves.
- **Script platform.** Node's platform pool is shared by every resource's
  isolate; it now grows with the machine instead of staying at four.

## Scripts on their own thread

Every runtime is created, run and freed on the script thread. This is how
the server works; there is no setting to turn it off. A script that computes for 300 ms holds up the
other scripts and nothing else: the tick asks for the world and takes it at
the script's next **safe point**, replicates, and gives it back.

A safe point is between two tasks of the script thread, or between two
instructions of a running script (V8 interrupts it at a loop edge or a
function call; the script keeps its place and carries on afterwards). So:

- A script's native call is never interrupted. It runs with the world from
  start to end, exactly as it did inside the tick.
- Between two statements of a script, a tick may have run. An entity looked
  up on one line may be gone on the next, as it always could be after an
  `await`. Bindings look entities up by id on every call, so this is a
  stale answer, never a dangling one.
- No safe point is taken while native code that the script called is on the
  stack and has called back into script (an event handler run from
  `Events.emit`, a state-bag listener, an export of another resource, a
  wrapped object's constructor): that native code is in the middle of its
  work. The tick waits for that call to return and keeps asking meanwhile.
  V8 does not report the return, so each such call is remembered by where
  it was made from (how deep the stack was, and which call in which script
  was at the top of it): it is over once the stack is no deeper than that,
  or that place on the stack holds something else. Up to eight are
  remembered at once; past that, or made from deeper than 48 frames, no
  safe point is taken until the outermost call into the isolate returns.
- A script blocked inside a native call that takes long on its own
  (`fs.readFileSync` of a large file, `execSync`) cannot be interrupted
  either. Use the asynchronous forms.

### Raising events off the script thread

Code off the script thread is `PostUpdate` on the tick thread, and RPC
handlers and the `On*` overrides on the packet thread. It raises events the
way it always did:

```cpp
v8::Isolate *isolate = engine->GetIsolate();
v8::Locker locker(isolate);
v8::Isolate::Scope isolateScope(isolate);
v8::HandleScope handleScope(isolate);
v8::Local<v8::Context> context = engine->GetContext();
v8::Context::Scope contextScope(context);
std::vector<v8::Local<v8::Value>> args {WrapPlayer(isolate, player)};
resourceManager->GetEvents().EmitReserved(isolate, context, "playerDied", args);
```

or the same with an `EngineScope`. What changed is underneath:

- Off the script thread, `engine->GetIsolate()` and `GetContext()` name a
  runtime of the calling thread's own (`NodeEngine::CreateOffThreadRuntime`).
  It has the bindings a resource's runtime has, runs no script, and the
  script thread never enters it, so locking it waits for no script. The
  server creates two, `"tick"` and `"packet"`; a thread sees the one it bound
  with `NodeEngine::BindOffThreadRuntime`, and `"tick"` if it bound none, so
  two such threads do not wait for each other's isolate either.
- An emit made there (`EmitReserved`, `EmitReservedTo`, `EmitClient`) copies
  its arguments out of that isolate, the way arguments cross between two
  resources, and posts the dispatch to the script thread. It returns at
  once, with a promise that is already settled: nothing of the handlers
  comes back. An argument that cannot be copied (a promise, a cycle) is
  logged and the event is not raised.
- `EmitReservedSync` runs the dispatch on the script thread and waits for
  the handlers' veto. That waits for whatever script is running there to
  finish its task, so keep it for events whose answer the caller needs. It
  must not be called with a handle held (see "The rules" below). The
  caller lets go of its isolate for the wait, so a handler may call a
  function that was among the arguments. An event that could not be raised
  (an argument that cannot be copied) answers `false`: nothing that might
  have refused it saw it.

For anything else that ends in script, hand it over:

```cpp
PostToScripts([this, guid = data.guid] {
    // The script thread: an EngineScope, a resource's function, anything tick code may do.
    CallResourceHook(guid);
});
```

- `PostToScripts(task)` queues `task` for the script thread, which looks at
  its queue about every 2 ms between the scripts it runs. That is an
  interval, not a deadline: a task waits behind the tasks queued before it
  and behind a script that is in the middle of a long callback.
- `RunOnScriptThread(task)` waits for it. From the tick thread the world may
  have changed when it returns. Like any wait for scripts, not with a handle
  held.

The framework's own events go the same way: client events
(`Events.onClient`), `playerConnecting` and the admission that follows,
voice events, console commands and resource hot reload.

Starting, stopping or restarting a resource from the tick thread, and
calling a resource's function directly, go into the script side's runtimes.
That first puts the script thread out of whatever it is running
(`Scripting::RuntimeEntry`), which waits for the running script's task.

Two things are not supported:

- Entering a *resource's* isolate from the tick thread with V8's own scopes
  (a `v8::Locker` on an isolate taken from a handler or a stored function
  rather than from `engine->GetIsolate()`). The script thread may be stopped
  in the middle of that runtime.
- Calling into script from a binding that holds a handle. The script it
  calls asks the world for entities on the same thread, which is a nested
  acquire. A binding takes its handle, uses the entity, and lets go before
  it calls a JS function.
- Calling into script from native code while holding a pointer to something
  of the mod's own that the tick may free, without `Scripting::NoSafePoint`
  around the call. The framework's entries (`EngineScope`, calls between
  runtimes) hold safe points off by themselves.

### What it costs

- An event raised by the tick or by a packet handler reaches its handlers
  about 2 ms later when the script thread is idle, and later than that
  behind a long callback. Its arguments are copied once more on the way.
- A handler's answer to an asynchronous emit can no longer be read where it
  was raised; a synchronous emit waits for the script thread.
- Two more runtimes (`"tick"` and `"packet"`) are kept for the life of the
  server.
- The resource monitor charges a script for the time it stood at a safe
  point, so a script that was interrupted reads slower than it ran.
- `tick.scriptsMs` in the slow-tick warning is now the time the tick waited
  for scripts to reach a safe point.

## Packets on their own thread

The tick thread handles no packet. The packet thread
(`Networking::PacketThread`, owned by the server `Instance`) sleeps until
MafiaNet has something, then receives until nothing waits and handles each
packet as it comes. This is how the server works; there is no setting to
turn it off and no handler chooses its thread.

### What runs where

| Arrives | Runs on | Notes |
| --- | --- | --- |
| RPC, typed and raw | Packet | The handler is given the `Sender` and the `World` |
| Inbound entity state (transforms, fields, state bags) | Packet | Applied with the entity locked; `OnDeserialized` runs there |
| Connect, join, ready, disconnect | Packet | `OnPlayerConnect`, `OnPlayerReady`, `OnPlayerDisconnect` are called there with the `World` |
| Chat | Packet | `OnChatMessage`, `OnChatCommand`, with the `World` |
| Client script event (`Events.emitServer`) | Packet, then script | Decoded on the packet thread; `OnClientEvent` and the handlers run on the script thread |
| Connection request | Packet, then script | `playerConnecting` handlers run on the script thread |
| Voice frames | MafiaNet's thread | As before; `VoiceServer`'s public functions are safe from any thread |

There is one packet thread, so handlers run one at a time, in the order
their packets arrived. A slow handler delays the packets behind it and
nothing else: the tick keeps its rate and scripts keep running. A handler
that takes at least `slow-packet-handler-ms` (`server.json`, default 10, 0
turns it off) is logged, at most once a second, with the digest of its RPC
identifier; the name is not in the binary.

A typed handler is not called for a payload that was cut short: one whose
`T::Serialize` read past the end of what the client sent. The packet is
dropped, counted (`NetworkServer::ShortPayloadsDropped()`) and logged, at
most once a second. Only the read decides: `Serialize` runs once, reading;
quantized fields and bits left unread after the payload are fine. A
`Serialize` that reads optional fields "if there are any left" is cut short
by this rule when they are absent; send them always, or use a raw handler. A
raw handler (`RegisterRawRPC`) decodes the stream itself and answers for
what a short one decodes to. One limit: a single read of a megabyte or more
past the end (a length taken off the wire and believed) is not noticed, so a
payload that reads such a length checks it itself.

Handlers are registered before the packet thread starts, in `PostInit` or
`PostScriptInit`. The table they go in is read on the packet thread without
a lock, so a handler cannot be registered later.

Sends (`SendRPC`, `BroadcastRPC`, `SignalExcept`, `Sender::Reply`) are
callable from any thread, with or without a handle held.

At shutdown the packet thread is stopped first. The handler in progress
finishes; RPCs still queued behind it are dropped unhandled.

### Entity handles

`ReplicationManager::GetWorld()` gives the `Replication::World`
(`networking/replication/world.h`). An RPC handler and the packet-thread
`On*` overrides are passed it.

```cpp
using Framework::Networking::Sender;
using Framework::Networking::Replication::World;

net->RegisterRPC<VehicleHorn>([](const VehicleHorn &msg, Sender sender, World &world) {
    auto [player, vehicle] = world.Write<Human, Vehicle>(sender.Avatar(), msg.vehicleId);
    if (!player || !vehicle) {
        return;
    }
    vehicle->horn = msg.on;
});
```

- `world.Read<T>(id)` returns a `ConstRef<T>`, `world.Write<T>(id)` a
  `Ref<T>`. A handle is empty (`false`) when the id does not resolve or the
  entity is not a `T`; the match is on the exact registered type.
  `Read<NetworkEntity>(id)` gives the entity whatever its type.
- A handle holds the entity's lock until it goes out of scope, or until
  `Reset()` lets go of it early. Readers share it; a writer has it alone.
  Handles move and do not copy. Keep one in the scope that took it; what is
  kept across calls is the id.
- `handle.Id()` is the entity's network id, on a `Ref` and a `ConstRef`
  alike, and 0 for an empty handle.
- `world.Write<A, B>(a, b)` and `world.Read<A, B>(a, b)` take several at
  once, up to `World::kMaxAtOnce` (8), and return a tuple of handles. Each
  is empty or not on its own. An id asked for twice fills the first handle
  only.
- `world.ForEach<T>(fn)` visits every `T` that exists when the call starts,
  by ascending id, one lock at a time. `fn` is given a `Ref<T> &` and holds
  that entity, so it cannot ask the world for another.
  `world.ForEachRead<T>(fn)` is the same walk with a `ConstRef<T> &`, which
  shares each entity with other readers. In both, `fn` returns nothing, or a
  `Replication::Walk`: `Walk::Stop` ends the walk there.
- `world.Exists(id)` says whether an id names an entity, from the table of
  ids alone. It takes no entity lock, so it is allowed with a handle held,
  and the answer may be old by the time it is read.
- `world.Create<T>()` creates an entity and returns it held: no other
  thread reaches it, and the pass does not serialize it, before the creator
  lets go. Set it up inside that scope. `world.Create(typeId)` does the same
  for a type known only at run time. Both are empty for a type that is not
  registered.
- `world.Destroy(id)` returns `false` if the id did not resolve. When it
  returns `true` the id has stopped resolving, on every thread. The entity
  itself is deleted, and its destruction sent, once no handle holds it:
  a handle taken before the call stays valid until it is released. `Destroy`
  may be called with a handle held, on that entity or another.

An entity made on the packet thread belongs to no resource, so no resource
stop destroys it. One made by a script belongs to the resource whose script
made it, as before.

Effects that reach beyond the entity come in two forms. Called on the
handle, they are safe inside its scope: `entity->SetOwner(guid)`,
`entity->ForceState()`, `entity->SetVirtualWorld(world)`, and
`ReplicationManager::SetOwner`, `ForceState`, `SetVirtualWorld` and
`SetViewer` given a `Ref`, as well as `SetViewerFilter`, `SetViewerAdmitted`
and `SetStreamTarget` given `*entity`. Called with a network id
(`SetOwner(id, guid)`, `ForceState(id)`, `SetVirtualWorld(id, world)`), they
take the handle themselves and so must be called with none held.

`GetViewerId(guid)` and `GetViewerIds()` answer from a map kept by id and
are safe from any thread, inside a handle's scope or not.

### Registered state

Mod state that is not an entity and that a packet handler touches (a
destruction registry, a seat table) is registered with the world, one object
per type, before the server starts:

```cpp
void MyServer::PostInit() {
    auto &world = Framework::CoreModules::GetReplication()->GetWorld();
    world.RegisterState(std::make_unique<DestructionRegistry>());
}
```

It is then reached like an entity, without an id, alone or in the same call
as entities:

```cpp
auto registry = world.Write<DestructionRegistry>();
auto [player, registry] = world.Write<Human, DestructionRegistry>(sender.Avatar());
```

The world owns the object and keeps it for its own life. A type that was
not registered gives an empty handle. State that only the tick thread and
the scripts touch does not have to be registered: the handoff between those
two still protects it.

### `Sender`

An RPC handler is given the connection its packet came from
(`networking/sender.h`):

- `sender.Guid()` is the connection as a `MafiaNet::PeerGuid`, what
  `ownerGUID` and `SetViewer` use; `sender.Connection()` is the same
  connection as a `MafiaNet::RakNetGUID`, what `SendRPC` and `SignalExcept`
  take.
- `sender.Avatar()` is the network id of the entity the connection controls
  (its viewer), 0 when it has none. No entity has id 0, so passing it to
  `Read` or `Write` gives an empty handle.
- `sender.Reply(payload)` sends to this connection only.
- `sender.Drop(reason)` closes the connection. The reason is sent to the
  client as written. Packets the connection had already sent are still
  queued; their handlers may run after `Drop` returns.

A `Sender` is valid for the length of the handler. What is kept beyond it
is the guid.

### The rules

**Take everything a scope needs in one call.** Within one call the world
locks registered states in the order they were registered, then entities by
ascending id, so two threads that ask for the same things in different
orders cannot deadlock. That order exists within one call only. Asking the
world for a handle while the thread already holds one (`Read`, `Write`,
`ForEach`, `Create`) logs a critical line and aborts the server, in every
build type: a deadlock on a live server would be worse. Two `Read`s nested
abort like two `Write`s.

```cpp
// Aborts: the second call is made while `vehicle` is held.
auto vehicle = world.Write<Vehicle>(msg.vehicleId);
auto driver  = world.Read<Human>(vehicle->seats[0]);

// Read the id, let go, then take both.
uint64_t driverId = 0;
if (auto vehicle = world.Read<Vehicle>(msg.vehicleId)) {
    driverId = vehicle->seats[0];
}
auto [vehicle, driver] = world.Write<Vehicle, Human>(msg.vehicleId, driverId);
```

Between the two calls of the second form the vehicle may have changed.
Check again what the handler relies on.

**These abort too when called with a handle held**, because they take a
handle themselves or take a lock that comes before an entity's:

- the `ReplicationManager` functions that take a network id (above),
  `StreamingPeers` and `StartReplication`
  (`NetworkServer::PushReplicationConnection`);
- `DelegationManager::Pin`, `SetDeclined` and `UpdateNow`;
- `Events::EmitReservedSync`. The handlers that would answer ask the world
  for entities, and the one the caller holds would never be theirs. A
  handler that needs a veto lets go of its handles, asks, and takes them
  again.
- `Instance::RunOnScriptThread`, for the same reason: the task it waits
  for runs where scripts run and may need the entity held. Let go first
  (`handle.Reset()`), or use `PostToScripts`, which does not wait.

An asynchronous emit (`EmitReserved`, `EmitReservedTo`, `EmitClient`),
`PostToScripts` and `TaskScheduler::PostToOwner` are fine with a handle
held: they copy and queue. So is building the arguments: the scripting
wrapper of an entity (`Builtins::Entity` and what derives from it, so
`WrapScriptPlayer`, `WrapScriptEntity` and a mod's own handles) keeps the
id and asks the world for nothing when it is constructed. A handler may
raise an event about the entities it holds, as mods always have:

```cpp
auto [player, vehicle] = world.Write<Human, Vehicle>(sender.Avatar(), msg.vehicleId);
if (!player || !vehicle) {
    return;
}
vehicle->seats[msg.seat] = sender.Avatar();
EventVehiclePlayerEntering(msg.vehicleId, sender.Avatar(), msg.seat); // wraps both ids for scripts
```

A wrapper made for an id whose entity is gone is not an error either: its
methods resolve when they are called and answer with nothing. Only a script
writing `new Entity(id)` itself is refused an id that names no entity
(`Entity::ConstructFromScript`, for a mod's own handle classes too).

**Convert what a script passed in before taking a handle.** A binding that
receives script values (`v8pp::from_v8`, reading a property of an options
object) runs script when it converts them: a getter, a `toString`, a proxy.
That script may ask the world for an entity, and with a handle held that is
a nested acquire, which aborts. Read every argument into plain C++ values
first, then take the handle.

**`world.Create` is refused with anything held**, a registered state
included. So "find it in my index, or create it" cannot be done under the
index's handle, and done in two steps it is not atomic: two handlers can
both miss and both create. When that matters, give the index a mutex of its
own instead of registering it as state, and hold that across the pair:

```cpp
uint64_t Garage::FindOrCreate(World &world, uint64_t ownerId) {
    const std::lock_guard guard(_mutex); // the mod's own; taken first, never under a handle
    if (const auto it = _vehicleOf.find(ownerId); it != _vehicleOf.end()) {
        return it->second;
    }
    auto vehicle = world.Create<Vehicle>();
    if (!vehicle) {
        return 0;
    }
    _vehicleOf[ownerId] = vehicle.Id();
    return vehicle.Id();
}
```

Such a mutex comes before every handle: take it with nothing held, and do
not take it inside a handle's scope elsewhere.

**A cache of your own needs a lock of its own.** A map a handler fills and
the tick or a script binding reads (a per-isolate class table, a lookup by
name) is reached from several threads now. Guard it with a mutex, held for
the lookup only and never while script runs, or register it with the world.

**Hooks on an entity run with that entity locked.** The Framework holds the
entity's lock around every extension point it calls on it: the serialize
functions, `OnDeserialized`, `GetInterestDependency`, `GetDelegationPolicy`,
`CanReleaseSimulation`, `OnSimulatorChanged`. An override works on its own
entity freely and must not ask the world for a handle. What it has to do on
another entity, it hands to `World::WhenNoneHeld`, which runs it on the
same thread once the Framework has let go:

```cpp
void Human::OnDeserialized(bool) {
    auto *replication = Framework::CoreModules::GetReplication();
    if (!replication->IsServer()) {
        return;
    }
    World::WhenNoneHeld([replication, self = GetNetworkID(), vehicleId = data.vehicleId] {
        if (auto vehicle = replication->GetWorld().Write<Vehicle>(vehicleId)) {
            vehicle->lastDriver = self;
        }
    });
}
```

Capture ids, not `this`: the entity may be gone when the task runs.

**Callbacks are told by id, with nothing held.** A state-change handler
(`AddStateChangeHandler`) is called once the writer has let go of the
entity, on the writer's thread, with `StateChange::networkId`. A
simulator-changed handler (`AddSimulatorChangedHandler`) is called once the
delegation pass is over, with the entity's network id. Both ask the world
for the entity, and the id may no longer resolve.

**A handler does not capture a mod object by pointer and use it bare.**
What it touches is an entity, registered state, or something that guards
itself (a mutex of its own, an atomic). `this` captured to call
`PostToScripts` is fine; `this->_registry` read on the packet thread while
`PostUpdate` writes it is a data race.

### Consistency is per entity

A handler that writes a vehicle and a player holds both locks while it
does, so no other thread sees one changed and not the other. The
replication pass, however, serializes one entity at a time and does not
stage changes: it may serialize the vehicle before the handler ran and the
player after. A client can therefore receive the two changes one pass
apart. This is FiveM's behaviour as well. Code on the client that needs
both treats the second as possibly late.

### What it costs

- Every entity access takes a lock: a lookup under the world's mutex, then
  the entity's `std::shared_mutex`. A handle waits while the pass serializes
  that entity, and the pass waits while a handle holds it; neither waits
  for the rest of the pass.
- Handlers run one at a time. One slow handler delays every packet behind
  it, from every connection.
- Inbound entity state waits only for its own entity. Construction and
  destruction messages from clients, connections opening and closing,
  `world.Create`, the teardown that follows `world.Destroy`, and the
  functions that walk connections (`StreamingPeers`,
  `StartReplication`, a delegation pass) wait for a replication pass that
  is running.
- The automatic replication worker count is one lower than before on the
  same host: the packet thread has a core.
- One more scripting runtime, and the 2 ms an event takes to reach the
  script thread now applies to events raised by RPC handlers.
- Nothing here has been measured under load yet.

## What still runs on the tick thread

Completions and tick-thread routes, voice and text-label upkeep, the
interest rebuild, delegation, the state-bag flush, the replication pass and
`PostUpdate`. Code there reaches entities through handles like everyone
else.

Each resource has its own isolate, and all of them share the script thread,
as in FiveM and MTA:SA: one slow script still delays the other scripts.
Long script work belongs in a `worker_threads` worker.

## Measuring

`FrameworkReplicationBench` reports the replication half of a tick.
`FW_BENCH_WORKERS=0,3,12` picks the worker counts:

```
FW_BENCH_WORKERS=0,3,-1 build/bin/FrameworkReplicationBench 250 500 1000
```

On a live server the slow-tick warning says where a tick's time went
(`network` is what is left of the tick's networking beside interest and the
pass: delegation and the state-bag flush; no packet is handled in a tick),
and Tracy shows `Server::TickThreadTasks`, `Replication::CollectInterest`
and the pass. The packet thread is the Tracy thread `Packets`; a handler
that holds it is reported by the slow-handler warning.

The packet thread has not been measured under load. The tests check that
the tick keeps its cadence while a handler sleeps for 900 ms (the longest
gap between two ticks must stay under 300 ms), that inbound state is
applied while the pass serializes, and that connect and disconnect storms
leave nothing behind. They print timings and assert no throughput.
