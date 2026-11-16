# Packet handling on a network thread

Status: draft for approval. Nothing here is built.

## Intent

The server follows FiveM's threading model, taken as the reference:

| FiveM | Framework | State |
|---|---|---|
| `svMain`: every resource, script events | script thread | done (PR #398) |
| `svSync`: sync tick, parallel per client | tick thread and replication workers | done (PR #398) |
| `svNetwork`: receives packets, runs packet handlers, queues script events | **packet thread** | this design |

Today the tick thread drains `Receive()` and runs every RPC handler and all inbound entity state
inside the tick (`NetworkPeer::Update`). A burst of packets, or one slow handler, lengthens the tick
for everyone. After this work the tick thread never handles a packet.

There is one model for the Framework and for every mod. No handler chooses its thread, and there is
no compatibility path. Mods are adapted during the transition.

## Goals

- Everything that arrives from the wire is handled on the packet thread: RPC handlers, inbound
  entity state, connection and disconnection, voice relay.
- Code that touches an entity cannot do so without holding that entity's lock, on any thread. The
  types enforce it; a convention does not.
- Handlers cannot deadlock each other or the tick.
- A ThreadSanitizer run of the threading test modules is part of CI on Linux.

## Non-goals

- The client. It keeps its single thread and its current API where that is possible (see "Shared
  code").
- A thread per resource. FiveM runs all resources on one thread; so do we.
- More than one packet thread. FiveM has one; handlers therefore run one at a time, in arrival order.

## Anti-goals

- An opt-in or opt-out, a "legacy" registration, or a flag. One way only.
- A handler API that works by posting to the tick thread by default. That is the model being left.
- Raw `NetworkEntity *` reachable from mod code on the server.

## Constraints

- MafiaNet's `Receive()` runs every plugin's `Update()` and then each plugin's `OnReceive()`.
  ReplicaManager3 is a plugin: its inbound path (`OnReceive`) and the outbound pass must become
  callable from two different threads at once.
- MafiaNet changes go upstream as a MafiaNet PR and reach the Framework through `cmake/MafiaNetPin.cmake`.
  During development the Framework builds against a local MafiaNet (`FW_MAFIANET_ROOT`).
- C++23 limited to what MSVC, GCC 13 and Apple clang share; no C++23 library types in SDK headers.
- Work stays on branch `server-multithreading` (PR #398).

## Approaches considered

1. **Locked entity handles (chosen).** A handler asks the world for the entities it needs and gets
   handles that hold each entity's lock for their scope. FiveM's model (per-entity locks on the
   network thread), enforced by types.
2. **One world lock.** Handlers keep their code and run on the packet thread under a lock shared
   with the tick. Smallest change for mods, but handler and tick still wait for each other: FiveM's
   threads without its concurrency. Rejected.
3. **Entities as mailboxes.** A handler posts a mutation that the entity applies later. No locks to
   get wrong, but a handler cannot read a value and act on it in the same call, which most real
   handlers do. Rejected.

The owner picked 1, and ruled out any per-handler choice of thread.

## Design

### Threads

- **Packet thread** (new, owned by `NetworkPeer` on the server). Loops: wait for packets, call
  `Receive()` until empty. All plugin `Update()` and `OnReceive()` calls happen here, except the
  replication pass.
- **Tick thread.** Interest rebuild, delegation, state-bag flush, the replication pass, mod
  `PostUpdate`. It no longer calls `Receive()`.
- **Script thread.** Unchanged. `WorldLock` still hands the world between tick and script. The
  packet thread does not take part in `WorldLock`; it is safe against both through entity locks.

### MafiaNet changes (one PR)

- A blocking wait for inbound packets (signalled when the return queue is pushed), so the packet
  thread sleeps instead of polling. `RakPeer` has nothing of the kind today.
- `Receive()` itself needs no change to split the pass out: the Framework already overrides
  `ReplicationManager::Update` and gates it (`BeginNetworkUpdate`).
- ReplicaManager3: the outbound pass is callable by its owner from another thread than the one
  calling `Receive()`, and is safe against `OnReceive`. Required property: inbound state for a
  connection waits only while that same connection is being serialized, never for the whole pass.
  The mechanism (per-connection lock is the expected one) is the builder's. `m_WorldListMutex`
  cannot serve: ReplicaManager3 holds it for the whole of `Update()`.
- Whatever else `Receive()` on one thread and sends from others turns out to need. `Send` is
  already designed to be called from any thread; the builder verifies RPC4's send path.

### The world and its handles

A new server-side type, `Replication::World`, is the only way to reach entities.

```cpp
net->RegisterRPC<VehicleAction>([](const VehicleAction &msg, Sender sender, World &world) {
    auto [player, vehicle] = world.Write<Human, VehicleEntity>(sender.Avatar(), msg.vehicleId);
    if (!player || !vehicle) return;
    vehicle->seats[msg.seat] = player->GetNetworkID();
    sender.Reply(UIVehicleWantedLevel {vehicle->wantedLevel});
    Scripting::Vehicle::EventVehiclePlayerEntering(msg.vehicleId, player->GetNetworkID(), msg.seat);
});
```

- `world.Read<T>(id)` and `world.Write<T>(id)` return a handle: empty when the id does not resolve
  or the entity is not a `T`. A handle holds the entity's lock until it goes out of scope. Handles
  cannot be copied or stored; what is kept across calls is the id.
- `world.Read<A, B>(a, b)` / `world.Write<A, B>(a, b)` take several entities at once, locked in a
  fixed order. Asking for a second handle while one is held, other than this way, is a programming
  error caught by an assertion in every build type.
- `world.ForEach<T>(fn)` visits every `T`, one lock at a time.
- `world.Create<T>(...)` and `world.Destroy(id)` are callable from any thread. After `Destroy`
  returns the id no longer resolves; an entity created becomes visible to clients at the next pass.
  A handle held on an entity keeps it alive until released.
- **Mod state that is not an entity** (M2O's destruction registry, prop records, jukeboxes, the
  vehicle part table) is registered with the world by type and reached the same way:
  `world.Write<DestructionRegistry>()`, and together with entities in one call:
  `world.Write<Human, DestructionRegistry>(id)`. One mechanism for everything a handler touches.
  In this step, everything a packet handler or a packet-thread `On*` override touches must be an
  entity or registered state; a handler capturing a mod object by pointer and using it directly is
  what the migration removes.
- `Sender` names the connection the packet came from: its guid, its avatar id, `Reply(payload)`,
  and a way to drop it.

The same handles are how the tick thread and the script thread reach entities: the replication
pass takes each entity's lock while it serializes it, interest rebuild reads positions under it,
and script bindings resolve their entity through `world` on every call (they already hold ids and
resolve per call, so this is mechanical). `GetEntityByNetworkID`, `ForEachEntity`, `GetViewer` and
the other raw-pointer accessors leave the server's public API.

Manager-wide effects made while a handle is held (`SetOwner`, `ForceState`, `SetVirtualWorld`,
`SetViewer`, marking a state bag dirty) are safe to call from any thread; they lock internally.

**Consistency across entities is per entity, not per pass**, as in FiveM. A handler that writes a
vehicle and a player holds both locks while it does, but the pass serializes one entity at a time:
a client may receive the vehicle's new state in one pass and the player's in the next. Mutations
are not staged to the pass.

### What runs where

| Arrives | Runs on | Notes |
|---|---|---|
| RPC (typed and raw) | packet thread | handler gets `Sender` and `World` |
| Inbound entity state (transform, fields) | packet thread | applied under the entity's lock |
| Client script event (`EmitScriptEvent`) | packet thread: rate-limit and decode, then queued to the script thread | FiveM's `QueueEvent`; no hop through the tick |
| Connect, disconnect, admission | packet thread | mod `On*` overrides are called there with `World`; their script events are queued |
| Voice frames | MafiaNet's own thread, as today | already off the tick (`voice_server.h`); `VoicePreference` and `VoiceTierRequest` handlers move to the packet thread, and `VoiceServer`'s public API, documented main-thread only, gets guarded |
| State-bag sync from a client | packet thread | under the entity's lock |

Sends (`SendRPC`, `BroadcastRPC`, `Sender::Reply`) are callable from any thread.

Raising a script event from the packet thread uses what PR #398 built: arguments are copied and the
dispatch is posted to the script thread. PR #398 gave the tick thread one runtime of its own to
build those arguments in; that becomes **one such runtime per thread that is not the script
thread**, so the packet thread and the tick thread never wait for each other's isolate lock. A
handler may therefore raise an event while it holds a handle, as mods do today. `EmitReservedSync` from the packet thread waits for the
script thread and must not be called while a handle is held (asserted): a handler that needs a
veto releases its handles, asks, and takes them again.

A handler may post work to the tick thread or the script thread (`TaskScheduler::PostToOwner`,
which exists, and `PostToScripts`),
the equivalents of FiveM's `gscomms_execute_callback_on_sync_thread` and `..._on_main_thread`. This
is for work that belongs there, not a way around the handles.

### Shared code

Entity classes and RPC payloads are shared with the client. The lock lives beside the entity on
the server, not in a field the client pays for. One shared virtual changes on both sides:
`NetworkEntity::GetInterestDependency()` returns the dependency's network id instead of a raw
pointer, and the Framework resolves and locks it. Any other place where an entity class reaches
another entity through a pointer gets the same treatment. `RegisterRPC` on
the client keeps a single-threaded signature; the builder decides whether the client gets a
trivial `World` (no locks) so shared handler code can be written once, or keeps today's signature.

### Errors and limits

- A handler that throws or asserts behaves as today.
- A slow handler delays the packets behind it and nothing else. A warning is logged when one
  exceeds a threshold, like the slow-tick warning.
- The packet thread appears in the `Threads:` line and in the profiler.

## Order of work

1. MafiaNet PR. Framework develops against it locally.
2. `World`, handles and `Sender`, with the tick thread still dispatching. Every Framework-internal
   entity access moves to handles; raw accessors are removed. Pure refactor: all tests stay green.
3. Dispatch moves to the packet thread.
4. Docs (`docs/server_threading.md`, a migration guide for mods), TSan job, MafiaNet pin, VERSION.

## Testing

- Unit tests for handles: empty on unknown id and on wrong type, ordered multi-acquire, destroy
  while held, the nested-acquire assertion, non-entity state.
- Whole-server tests with a loopback client: RPCs flooding while the tick runs and a script
  computes; inbound state applied while the pass serializes; connect and disconnect storms;
  handler order preserved per connection.
- Measurement: tick time under a packet flood, before and after.
- ThreadSanitizer in CI (Linux) on the modules that run no V8: the handle tests and the whole-server
  network tests. That job builds MafiaNet from source (`FW_MAFIANET_ROOT`) so its locks are
  instrumented; libnode is prebuilt and cannot be, which is why the script-thread module is left out.
- M2O: its server handlers and bindings are ported in a temporary copy to prove the migration and
  to re-run the earlier before/after comparison. The M2O repository is not touched; the port is
  offered as a PR only if asked for.

## Consequences

- MAJOR version bump: every server RPC handler and every server-side entity access in every mod
  changes. M2O has 34 server handlers.
- PR #398 cannot merge until the MafiaNet release it pins exists.

## Calls made by the author, for the owner to check

- Consistency is per entity, not per pass (FiveM's behaviour).
- One argument-building runtime per non-script thread.
- `GetInterestDependency()` returns an id, on client and server.
- All mod state a handler touches is registered with the world in this step.
- The TSan job covers the non-V8 modules and builds MafiaNet from source.

## Left to the builder

- Lock type and where it lives; how the fixed lock order is defined.
- How `Create` and `Destroy` are carried to the pass.
- The ReplicaManager3 locking mechanism, within the property stated above.
- Names, file layout, slow-handler threshold.

## Open decision for the owner

FiveM has no equivalent of `WorldLock`: `svMain` and `svSync` run at the same time and rely on
entity locks alone. Once every entity access goes through handles, the same becomes possible here:
scripts would run during the tick, and the safe-point machinery from PR #398 could go. This design
keeps `WorldLock` for now: state that only the tick and the scripts touch (never a packet handler)
does not have to be registered in this step and is still protected by it. Removing `WorldLock` means
registering that state too, and is proposed as the step after this one.
