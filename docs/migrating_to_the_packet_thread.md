# Migrating a mod to the packet thread

What a mod's server code has to change now that packets are handled on a
thread of their own and entities are reached through locked handles. The
model itself is in [server_threading.md](server_threading.md); read "The
rule" and "Packets on their own thread" there first. This page is the list
of changes, each with the code before and after.

The change is a MAJOR version: every server RPC handler and every
server-side entity access changes. There is no compatibility path and no
setting. The client keeps its single thread; what changes for it is listed
in section 9.

The examples use a mod's own types (`Human`, `Vehicle`, `VehicleAction`).
The aliases they assume:

```cpp
using Framework::Networking::Sender;
using Framework::Networking::Replication::ConstRef;
using Framework::Networking::Replication::Ref;
using Framework::Networking::Replication::World;
```

## What to look for

Three things break at compile time and one does not.

- A server `RegisterRPC` handler taking `MafiaNet::Packet *` no longer
  compiles: that form is `NetworkClient`'s alone, and neither
  `NetworkServer` nor the `NetworkPeer *` from
  `CoreModules::GetNetworkPeer()` has it.
- `GetEntityByNetworkID`, `GetEntity<T>`, `ForEachEntity`, `ForEach<T>`,
  `GetViewer`, `GetViewerAs<T>`, `ForEachViewer`, `CreateEntity` and
  `DestroyEntity` are gone from `ReplicationManager`'s public interface.
  ReplicaManager3 is now a private base, so its own accessors
  (`GetConnectionByGUID`, the replica lists) are gone with them.
- The `On*` overrides, `GetInterestDependency`, simulator-changed handlers
  and `StateChange` changed signature or fields.
- **Not caught by the compiler:** a handler that reads or writes a mod
  object through a captured pointer. It compiles, it now runs on the packet
  thread, and it races with `PostUpdate` and with scripts. Section 3.

Two failures show up only when the code runs, as a critical log line
followed by an abort: asking the world for a handle while holding one, and
`EmitReservedSync` or `RunOnScriptThread` with a handle held. So does
registering an RPC handler or a state after the packet thread has started.
Each names what was attempted. Run
every handler once after porting it.

## 1. A server RPC handler

Before, the handler ran in the tick and resolved pointers:

```cpp
netServer->RegisterRPC<VehicleAction>([netServer](const VehicleAction &msg, MafiaNet::Packet *packet) {
    auto *player  = ViewerHuman(MafiaNet::ToPeerGuid(packet->guid));
    auto *vehicle = ResolveVehicle(msg.vehicleId);
    if (!player || !vehicle) {
        return;
    }
    const uint64_t playerId = player->GetNetworkID();
    if (msg.seat < Vehicle::kMaxSeats) {
        vehicle->seats[msg.seat] = playerId;
        if (msg.seat == 0) {
            vehicle->SetOwner(MafiaNet::ToPeerGuid(packet->guid));
            UIVehicleWantedLevel level;
            level.level = vehicle->wantedLevel;
            netServer->SendRPC(level, packet->guid);
        }
    }
    EventVehiclePlayerEntering(msg.vehicleId, playerId, msg.seat);

    VehicleAction relayed = msg;
    relayed.networkId     = playerId;
    MafiaNet::BitStream bs;
    relayed.Serialize(&bs, true);
    netServer->SignalExcept(VehicleAction::kIdentifier, bs, packet->guid);
});
```

After, it is given the sender and the world, and takes both entities in
one call:

```cpp
netServer->RegisterRPC<VehicleAction>([netServer](const VehicleAction &msg, Sender sender, World &world) {
    // The player's id is the sender's avatar: no need to take the player to read it.
    const uint64_t playerId = sender.Avatar();
    auto [player, vehicle]  = world.Write<Human, Vehicle>(playerId, msg.vehicleId);
    if (!player || !vehicle) {
        return;
    }
    if (msg.seat < Vehicle::kMaxSeats) {
        vehicle->seats[msg.seat] = playerId;
        if (msg.seat == 0) {
            vehicle->SetOwner(sender.Guid());
            UIVehicleWantedLevel level;
            level.level = vehicle->wantedLevel;
            sender.Reply(level);
        }
    }
    // Wraps both ids for scripts and queues the event: fine with the handles held.
    EventVehiclePlayerEntering(msg.vehicleId, playerId, msg.seat);

    VehicleAction relayed = msg;
    relayed.networkId     = playerId;
    MafiaNet::BitStream bs;
    relayed.Serialize(&bs, true);
    netServer->SignalExcept(VehicleAction::kIdentifier, bs, sender.Connection());
});
```

What changed, line by line:

| Before | After |
| --- | --- |
| `MafiaNet::Packet *packet` | `Sender sender, World &world` |
| `packet->guid` | `sender.Connection()` |
| `MafiaNet::ToPeerGuid(packet->guid)` | `sender.Guid()` |
| `replication->GetViewer(guid)` | `sender.Avatar()` (an id), or `replication->GetViewerId(guid)` |
| `net->SendRPC(payload, packet->guid)` | `sender.Reply(payload)` |
| `net->KickPlayer(packet->guid, ...)` | `sender.Drop(reason)`, or `KickPlayer` as before |
| two lookups, two pointers | one `world.Write<A, B>(a, b)`, two handles |

`RegisterRawRPC` on the server changes the same way: its handler is
`(MafiaNet::BitStream *, Sender, World &)`.

Things to check in every handler:

- **Lookup helpers that return pointers go.** A helper cannot return one
  handle and let the caller take a second: that is a nested acquire. Helpers
  return ids (`sender.Avatar()` is the usual one) and the handler takes
  everything in one call. A helper may be passed `*vehicle` or a handle by
  reference and work on it, provided it asks the world for nothing.
- **An entity found through another needs two steps.** Read the id under
  one handle, let go, take both, and check again what the handler relies on:

  ```cpp
  uint64_t vehicleId = 0;
  if (auto player = world.Read<Human>(sender.Avatar())) {
      vehicleId = player->data.vehicle.vehicleId;
  }
  auto [player, vehicle] = world.Write<Human, Vehicle>(sender.Avatar(), vehicleId);
  if (!player || !vehicle || player->data.vehicle.vehicleId != vehicleId) {
      return;
  }
  ```

- **Events may be raised with the handles held, entity wrappers included.**
  An asynchronous emit copies and queues, and the scripting wrapper of an
  entity (`Builtins::Entity` and what derives from it) only keeps the id:
  it asks the world for nothing when it is constructed, and does not throw
  for an entity that has gone. A mod's own handle class must keep to that:
  its native constructor takes the id and nothing else, and its script
  constructor is `Entity::ConstructFromScript<T>`, which is what refuses an
  unknown id written in a script.
- **`EmitReservedSync` and `RunOnScriptThread` are called with no handle
  held.** Both wait for scripts, which may need the entity; both abort
  otherwise. A handler that needs a veto lets go (`Reset()`, or the end of
  the scope), asks, and takes its handles again.
- **Script arguments are converted before a handle is taken.** Converting a
  script value can run script (a getter, a `toString`), which may ask the
  world for an entity: with a handle held, that aborts. Read the arguments
  into C++ values, then take the handle.
- **The type match is exact.** `world.Write<T>(id)` is empty unless the
  entity was registered as `T` itself. Code that used `dynamic_cast` to
  reach a base class asks for the concrete type, or for `NetworkEntity`.
- **A truncated payload never reaches a typed handler.** The server drops
  a packet whose `Serialize` read past the end of what was sent, and only
  that: quantized fields and unread trailing bits are fine, and `Serialize`
  runs once. A payload that reads optional trailing fields when "there is
  more" is dropped when they are absent; send them always, or register a
  raw handler. The log line says "payload was cut short" with the RPC's
  digest, and `NetworkServer::ShortPayloadsDropped()` counts them.
- **Handlers are registered in `PostInit` or `PostScriptInit`.** The packet
  thread starts at the end of `Init`; registering after that is a data race
  on the handler table.
- **A handler that takes long delays every packet behind it.** Above
  `slow-packet-handler-ms` (default 10) it is logged. Blocking work goes to
  `TaskScheduler::RunInBackground` as before.

## 2. A script binding resolving an entity

Before, a binding resolved a pointer and used it:

```cpp
VehicleEntity *Vehicle::ResolveVehicle() const {
    return dynamic_cast<VehicleEntity *>(Resolve());
}

void Vehicle::SetWantedLevel(int level) {
    if (auto *v = ResolveVehicle()) {
        v->wantedLevel = level;
    }
}
```

After, `Builtins::Entity::Resolve()` returns a handle
(`Ref<NetworkEntity>`), and `ResolveAs<T>()` returns one of the concrete
type:

```cpp
Ref<VehicleEntity> Vehicle::ResolveVehicle() const {
    return ResolveAs<VehicleEntity>();
}

void Vehicle::SetWantedLevel(int level) {
    if (auto v = ResolveVehicle()) {
        v->wantedLevel = level;
    }
}
```

Most bindings change by one token (`auto *v` becomes `auto v`). The ones
that need thought:

- A binding that resolves a second entity while it holds the first. Read
  the id, let go, then take both with `world.Write<A, B>`, or read what is
  needed from each in turn:

  ```cpp
  // Before
  auto *v     = ResolveVehicle();
  auto *human = dynamic_cast<HumanEntity *>(repl->GetEntityByNetworkID(v->seats[seat]));

  // After
  uint64_t occupant = 0;
  if (auto v = ResolveVehicle()) {
      occupant = v->seats[seat];
  }
  auto human = repl->GetWorld().Read<HumanEntity>(occupant);
  ```

- A binding that calls a JS function, converts a script value, or raises a
  synchronous event while it holds a handle. Convert first, and let go
  before the call. Constructing a script wrapper of an entity is fine with a
  handle held.
- A binding that returned or stored the pointer. A handle is kept in the
  scope that took it; store the id.
- `Entity::GetHandle()` returns a handle too. `Entity::SetVisibleTo` takes
  the target connection's `MafiaNet::PeerGuid`; `Entity::ResolveOwner(owner)`
  reads it from the target without holding it afterwards.
- `Player::ResolveIdentity()` returns `std::optional<ClientIdentity>`
  (section 8). `Player::ResolveNametag()` and `WritableNametag()` return a
  `HeldNametag`, which keeps the entity held and is used like the pointer
  was (`if (auto tag = ResolveNametag()) tag->...`).
- `EntityCollection::ForEachNative` gives network ids, not pointers.

A script still sees a stale handle as before: a property reads its default
and a setter does nothing.

## 3. Mod state that is not an entity

Before, a handler captured the service and used it:

```cpp
netServer->RegisterRPC<PropBroken>([this](const PropBroken &msg, MafiaNet::Packet *) {
    _destruction.MarkBroken(msg.propId);
});
```

That object is also used by `PostUpdate` and by script bindings, which run
on other threads. Register it with the world, once, before the server
starts:

```cpp
void MyServer::PostInit() {
    auto &world = Framework::CoreModules::GetReplication()->GetWorld();
    world.RegisterState(std::make_unique<DestructionRegistry>());
}
```

and reach it by type everywhere, alone or together with entities:

```cpp
netServer->RegisterRPC<PropBroken>([](const PropBroken &msg, Sender sender, World &world) {
    auto [player, destruction] = world.Write<Human, DestructionRegistry>(sender.Avatar());
    if (!player || !destruction) {
        return;
    }
    destruction->MarkBroken(msg.propId);
});
```

- There is one object per type, owned by the world for its life. Split a
  large service into the state (registered) and the code that works on it.
- `PostUpdate` and script bindings reach the same object through
  `GetWorld().Write<DestructionRegistry>()`, under the same rules: one call
  for everything, no nested acquire.
- States are locked in the order they were registered, before entities.
- State that only `PostUpdate` and scripts touch may stay a plain member.
  State that only packet handlers and the packet-thread `On*` overrides
  touch has one thread today, because there is one packet thread. Register
  it anyway: the first binding or tick service that reads it is a race.
- Small things that guard themselves (a `std::atomic` counter, a map behind
  its own mutex that is never held across a call) need not be registered.

## 4. The `On*` instance overrides

Five overrides gained a `World &` and moved to the packet thread:

```cpp
// Before
void OnPlayerConnect(const PlayerConnectionData &data) override;
void OnPlayerReady(MafiaNet::PeerGuid guid) override;
void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;
void OnChatMessage(uint64_t senderNetworkId, const std::string &text) override;
void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command,
    const std::vector<std::string> &args) override;

// After
void OnPlayerConnect(const PlayerConnectionData &data, World &world) override;
void OnPlayerReady(MafiaNet::PeerGuid guid, World &world) override;
void OnPlayerDisconnect(MafiaNet::PeerGuid guid, World &world) override;
void OnChatMessage(uint64_t senderNetworkId, const std::string &text, World &world) override;
void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command,
    const std::vector<std::string> &args, World &world) override;
```

An override left with the old signature no longer overrides anything; the
`override` keyword turns that into a compile error. They are called with no
handle held. `Instance::HandleIncomingChat` takes the world as well.

A typical connect, before and after:

```cpp
// Before
void MyServer::OnPlayerConnect(const PlayerConnectionData &data) {
    auto *replication = Framework::CoreModules::GetReplication();
    auto *player      = replication->CreateEntity<Human>();
    player->nickname  = data.nickname;
    player->ownerGUID = data.guid;
    replication->SetViewer(data.guid, player);
    _players.Add(data.guid, player);
}

// After
void MyServer::OnPlayerConnect(const PlayerConnectionData &data, World &world) {
    auto *replication = Framework::CoreModules::GetReplication();
    uint64_t id       = 0;
    {
        auto player = world.Create<Human>();
        if (!player) {
            return;
        }
        player->nickname = data.nickname;
        replication->SetOwner(player, data.guid);
        replication->SetViewer(data.guid, player);
        id = player->GetNetworkID();
    }
    if (auto players = world.Write<PlayerDirectory>()) {
        players->Add(data.guid, id);
    }
}
```

`world.Create` cannot be called with a handle held, a registered state's
included, so the directory is taken after the new entity is released, or
before it is created. That makes "look it up, or create it" two steps, and
two handlers can both miss. Where that matters, keep the index outside the
world behind a mutex of the mod's own, taken with nothing held and kept
across the lookup and the `Create` (see "The rules" in
`docs/server_threading.md` for the example).
`World::Archetypes::PlayerArchetype::SetupServer` and
`StreamingArchetype::SetupServer` take the handle (`SetupServer(player,
data.guid)`).

In `OnPlayerDisconnect` the avatar still resolves:
`replication->GetViewerId(guid)` gives its id, and the Framework destroys
it after the override returns.

`OnPlayerDisconnect` is called only for a player that reached ready
(`OnPlayerReady` was called for it). A connection that drops between
`OnPlayerConnect` and ready gets no call; the Framework still destroys its
avatar. The directory above would keep that player's entry for ever, so
either keep what belongs to a player on the avatar entity, which goes with
it, or drop the entries whose entity is gone, from the tick:

```cpp
void MyServer::PostUpdate() {
    auto &world = Framework::CoreModules::GetReplication()->GetWorld();
    if (auto players = world.Write<PlayerDirectory>()) {
        // World::Exists takes no entity lock, so it is allowed with the directory held.
        players->EraseIf([&](uint64_t avatarId) {
            return !world.Exists(avatarId);
        });
    }
}
```

An override that throws is logged and the connection carries on: see "The
rules" in `docs/server_threading.md`.

`OnPlayerVoiceStateChanged`, `OnConsoleCommand` and `OnClientEvent` keep
their signatures and run on the script thread. `PostUpdate` stays on the
tick thread; the entities it touches are reached through
`Framework::CoreModules::GetReplication()->GetWorld()`.

`WrapScriptPlayer` and `WrapScriptEntity` keep their signatures and are
called on every thread that raises an event about an entity: the tick
thread, the packet thread (from a handler, with a handle held or not) and
the script thread, each in its own isolate. An override asks the world for
no handle and guards any cache of its own. To choose a wrapper class by
the entity's type it asks `world.TypeIdOf(id)`, which like `Exists` reads
the table of ids and takes no entity lock:

```cpp
v8::Local<v8::Value> MyServer::WrapScriptEntity(v8::Isolate *isolate, uint64_t networkId) {
    const uint32_t type = Framework::CoreModules::GetReplication()->GetWorld().TypeIdOf(networkId);
    if (type == Framework::Networking::Replication::EntityTypeId<Vehicle>()) {
        return v8pp::class_<ScriptVehicle>::create_object(isolate, networkId);
    }
    return Instance::WrapScriptEntity(isolate, networkId);
}
```

`ModuleRegister` is called for every runtime as it is set up, the tick and
packet threads' own included. Build the mod's handle classes there
(`ScriptVehicle::GetClass(isolate)`), so that wrapping one later is a
lookup. It runs under the lock that keeps binding declarations apart: it
asks the world for no handle and should run no script.

## 5. `GetInterestDependency` returns an id

This virtual is shared with the client, so the entity class changes on
both sides.

```cpp
// Before
Replication::NetworkEntity *GetInterestDependency() override {
    return data.vehicle.vehicleId != 0 ? ResolveSibling(data.vehicle.vehicleId) : nullptr;
}

// After
uint64_t GetInterestDependency() override {
    return data.vehicle.vehicleId;
}
```

0 means none, and an id that does not resolve counts as none.
`NetworkEntity::ResolveSibling` is removed: an entity class no longer
reaches another entity. The Framework calls this with the entity locked, so
the override reads its own fields and nothing else.

The same holds for every extension point the Framework calls on an entity
(`OnDeserialized`, the serialize functions, `GetDelegationPolicy`,
`CanReleaseSimulation`, `OnSimulatorChanged`): on the server they run with
that entity locked and must not ask the world for a handle. What an
override did to another entity moves into `World::WhenNoneHeld`:

```cpp
// Before
void Human::OnDeserialized(bool) {
    if (auto *vehicle = dynamic_cast<Vehicle *>(ResolveSibling(data.vehicle.vehicleId))) {
        vehicle->lastDriver = GetNetworkID();
    }
}

// After
void Human::OnDeserialized(bool) {
    auto *replication = Framework::CoreModules::GetReplication();
    if (!replication->IsServer()) {
        return;
    }
    World::WhenNoneHeld([replication, self = GetNetworkID(), vehicleId = data.vehicle.vehicleId] {
        if (auto vehicle = replication->GetWorld().Write<Vehicle>(vehicleId)) {
            vehicle->lastDriver = self;
        }
    });
}
```

The task runs on the same thread at that thread's next settle: the end of the packet drain
or of the tick's pass it is part of, or the next handle the thread lets go
of. Not at the moment the Framework lets go of the entity, which is still
under a connection's lock. Capture ids, not `this`.

## 6. Simulator-changed handlers and state changes carry ids

```cpp
// Before
delegation.AddSimulatorChangedHandler(
    [](NetworkEntity *entity, MafiaNet::PeerGuid previous, MafiaNet::PeerGuid current, DelegationChange change) {
        if (auto *npc = CheckedEntityCast<Npc>(entity)) {
            npc->OnHandover(current);
        }
    });

// After
delegation.AddSimulatorChangedHandler(
    [](uint64_t networkId, MafiaNet::PeerGuid previous, MafiaNet::PeerGuid current, DelegationChange change) {
        auto &world = Framework::CoreModules::GetReplication()->GetWorld();
        if (auto npc = world.Write<Npc>(networkId)) {
            npc->OnHandover(current);
        }
    });
```

The handler is called once the pass that made the change is over, on the
thread that ran it, with nothing held. The entity may have changed again or
be gone by then, so the id may not resolve. `UpdateNow` or `Pin` called
from inside the handler runs nothing at once; it asks for the next pass.

`StateChange::entity` is replaced by `StateChange::networkId`:

```cpp
// Before
replication->AddStateChangeHandler(filter, [](const StateChange &change) {
    Log(change.entity->GetNetworkID(), change.key);
});

// After
replication->AddStateChangeHandler(filter, [replication](const StateChange &change) {
    if (auto entity = replication->GetWorld().Read<NetworkEntity>(change.networkId)) {
        Log(entity->GetNetworkID(), change.key);
    }
});
```

On the server the handler is called once the writer has let go of the
entity, on the writer's thread, which may be the packet thread. A script's
`entity.state.onChange` listener and the `entityStateChange` event always
run on the script thread, later.

The rest of the delegation and replication API names entities by id or by
handle, never by pointer:

| Before | After |
| --- | --- |
| `Delegation().Pin(entity, guid)`, `Unpin(entity)` | `Pin(id, guid)`, `Unpin(id)` |
| `IsPinned(entity)`, `PinnedTo(entity)` | `IsPinned(id)`, `PinnedTo(id)` |
| `SetDeclined(entity, guid, declined)`, `IsDeclined(entity, guid)` | the same with the id |
| `const Stats &GetStats()` | `Stats GetStats()`, a copy |
| `ForceState(entity)`, `SetOwner(entity, guid)` | `ForceState(id)` / `ForceState(ref)`, `SetOwner(id, guid)` / `SetOwner(ref, guid)` |
| `SetViewer(guid, entity)` | `SetViewer(guid, id)` / `SetViewer(guid, ref)` |
| `MarkStateBagDirty(entity)` | `MarkStateBagDirty(id)` |
| `ForEachStreamingPeer(entity, fn)` | `for (PeerGuid peer : StreamingPeers(id))` |

Which of these may be called with a handle held:

- Not `ForceState(id)`, `SetOwner(id, guid)`, `SetVirtualWorld(id, world)`,
  `SetViewer(guid, id)`, `Pin`, `SetDeclined`, `UpdateNow` or
  `StreamingPeers`: they take a handle themselves, or ReplicaManager3's
  world, and abort.
- `MarkStateBagDirty(id)`, `Unpin(id)`, `IsPinned(id)`, `PinnedTo(id)`,
  `IsDeclined(id, guid)`, `GetViewerId(guid)`, `World::Exists(id)` and
  `World::TypeIdOf(id)` only touch a table under its own mutex: fine
  anywhere.
- The forms that take a `Ref`, and `entity->SetOwner(guid)` and
  `entity->ForceState()`, are for inside the handle's scope.

`StreamingPeers(id)` returns the list rather than calling back, so what is
done with each peer runs under nothing.

## 7. Creating and destroying entities

```cpp
// Before
auto *vehicle = replication->CreateEntity<Vehicle>();
vehicle->position = spawn;
vehicle->modelId  = model;
return vehicle->GetNetworkID();
...
replication->DestroyEntity(vehicle);

// After
World &world = replication->GetWorld();
auto vehicle = world.Create<Vehicle>();
if (!vehicle) {
    return 0;
}
vehicle->position = spawn;
vehicle->modelId  = model;
return vehicle->GetNetworkID();
...
world.Destroy(vehicleId);
```

- `Create` returns the entity held. No other thread finds it and the pass
  does not send it before the handle is released, so everything a client
  needs at construction is set inside that scope.
- `Create` aborts when the thread already holds a handle. Create first, or
  let go first.
- `Create(typeId)` replaces `CreateEntity(typeId)` and returns a
  `Ref<NetworkEntity>`.
- `Destroy` takes the id and returns whether it resolved. The id stops
  resolving at once. A thread that holds a handle on the entity, the caller
  included, keeps a valid entity until it lets go; the destruction is sent
  after that.
- `SetOnEntityDestroyed` fires when the entity is torn down, which may be
  later than the `Destroy` call and on another thread.
- An entity created on the packet thread belongs to no resource: stopping a
  resource does not destroy it. Entities created by a script binding belong
  to the calling resource as before.
- Iteration: `replication->ForEach<Vehicle>([](Vehicle *v) {...})` becomes
  `world.ForEach<Vehicle>([](Ref<Vehicle> &v) {...})`, or
  `world.ForEachRead<Vehicle>([](ConstRef<Vehicle> &v) {...})` for a walk
  that only reads. The callback holds `v` and cannot ask the world for
  anything else; collect ids (`v.Id()`) and work on them after the loop
  when it needs to. A search returns `Walk::Stop` from the callback once it
  has what it came for.
- `handle.Id()` gives the network id through a read handle too, and
  `handle.Reset()` lets go before the end of the scope.

## 8. Per-connection identity is returned by value

```cpp
// Before
const RPC::ClientIdentity *identity = net->GetPeerIdentity(guid);
if (identity == nullptr) {
    return;
}
Use(identity->nickname);

// After
const std::optional<RPC::ClientIdentity> identity = net->GetPeerIdentity(guid);
if (!identity) {
    return;
}
Use(identity->nickname);
```

The connection may close on the packet thread while another thread reads
its identity, so the caller gets a copy. Do not call it in a loop per
field; take it once.

Answers about connections (`IsAuthenticated`, `HasConnection`,
`GetViewerId`) are safe from any thread, and asked anywhere but the packet
thread they may already be old when they return.

## 9. Everything else

**A type of the mod named `World`.** Handlers now name
`Framework::Networking::Replication::World`. A mod that has its own `World`
class or a `...::World` namespace in scope (the Framework itself has the
namespace `Framework::World`) gets an ambiguous or wrong name inside its
handlers. Spell the type in full, or alias it once in a shared header:

```cpp
namespace MyMod {
    using EntityWorld = Framework::Networking::Replication::World;
}
```

**`TickBreakdown::packetsMs` is `networkMs`.** The tick handles no packet;
the field is what is left of the tick's networking beside interest and the
pass. The slow-tick warning prints it as `network` instead of `packets`.
Dashboards and log parsers keyed on the old name need the new one. Time
spent in RPC handlers is no longer in any tick figure; the slow-handler
warning reports it.

**The client keeps its API.** On the client `RegisterRPC<T>` still takes
`(const T &payload, MafiaNet::Packet *packet)` and there are no handles.
What moved is where entities are found: the pointer accessors left
`ReplicationManager` for `Replication::ClientEntities`
(`networking/replication/client_entities.h`).

```cpp
// Before
auto *human = replication->GetEntity<Human>(msg.networkId);
replication->ForEach<Vehicle>([](Vehicle *v) { v->Frame(); });

// After
const Framework::Networking::Replication::ClientEntities entities(*replication);
auto *human = entities.GetEntity<Human>(msg.networkId);
entities.ForEach<Vehicle>([](Vehicle *v) { v->Frame(); });
```

`ClientEntities` has `GetEntityByNetworkID`, `GetEntity<T>`,
`ForEachEntity`, `ForEach<T>` and `ForEachAvatar`. It is compiled into
`FrameworkClient` only: a server target that uses it does not link. A
client state-change handler finds its entity with
`entities.GetEntityByNetworkID(change.networkId)`.

**Shared code cannot register handlers once for both sides.** The two
signatures differ. Payload structs and entity classes stay shared; handler
registration is per side, as `project_architecture.md` already places it.

**Shared entity classes do not reach other entities.** `ResolveSibling` is
gone and neither `World` handles nor `ClientEntities` exist on both sides.
Code in `shared/` keeps ids; each side resolves them its own way.

**`NetworkPeer::GetNetworkIDManager()` is public on `NetworkClient` only.**
A server does not look entities up in the id table.

**`ReplicationManager` no longer exposes ReplicaManager3.**
`GetConnectionByGUID(guid) != nullptr` becomes `HasConnection(guid)`, and
replication to a peer is started with `StartReplication` (or
`NetworkServer::PushReplicationConnection`, as before). `GetConnectionCount`,
`SetAutoSerializeInterval`, `SetAutoManageConnections` and `Clear` remain.

**`VoiceServer::GetRouter()` is `WithRouter(fn)`.** The router is guarded
by the voice server's mutex; `fn` is given the router under it, and must
not call back into the voice server's setters or the world.

```cpp
// Before
const float range = voice->GetRouter().GetDefaultRange();
// After
const float range = voice->WithRouter([](auto &router) { return router.GetDefaultRange(); });
```

**A server that does not use `Integrations::Server::Instance`.**
`NetworkServer::Update` receives nothing any more. Something must drain
packets: a `Networking::PacketThread` started on the server, or a call to
`DrainPackets()` from one thread. `NodeEngine::CreateOffThreadRuntime` takes
a name, and a thread that raises events binds its own with
`BindOffThreadRuntime`; `Instance` creates `"tick"` and `"packet"` and binds
them.

**Fewer automatic replication workers.** `replication-threads: -1` now
leaves three cores aside instead of two. A host that was sized exactly
gets one worker fewer; pin the count in `server.json` to keep it.

**New `server.json` key.** `slow-packet-handler-ms`, default 10, 0 to turn
the warning off.

**Thread assumptions inside handlers.** Anything a handler used that was
"tick thread only" by convention needs another look: a non-atomic counter,
a cache, a `static` buffer, a library handle. A cache of the mod's own that
a handler fills and anything else reads needs a lock of its own. Work that belongs on the tick
thread is posted there with
`Framework::CoreModules::GetTaskScheduler()->PostToOwner(task)`, and work
that ends in script with `Instance::PostToScripts(task)`. Both run later;
neither is a way around the handles, which the task takes again when it
runs.

**An owner's destruction of its entity.** When a client destroys an entity
it owns, the mod's `PreDestruction` runs with the entity held by nobody
else. If a handler or a script holds the entity at that moment, the
destruction waits for it as before, and `PreDestruction` now waits too: it
runs at the teardown, with no connection named (`nullptr`).

**An entity belongs to the resource that created it, and to no other.** An
entity is attributed to a resource only when the creating thread is running
that resource's script. One a mod creates in `PostUpdate` or in a packet
handler belongs to no resource, whatever script was running at that moment.

**Two entities change one pass apart.** A handler that writes a player and
a vehicle holds both, but the pass serializes entities one at a time. A
client may see the vehicle's new seat in one update and the player's new
vehicle id in the next. Client code that needs both tolerates the gap.

## Behaviour changes

What changed without changing a signature, in one list:

- `NetworkServer::Update` no longer receives. Packets are handled by a
  `PacketThread`, or by whoever calls `DrainPackets`.
- A server state-change handler runs after the writer's handle is let go,
  on the writer's thread, and is given an id. A script's state-bag listener
  runs on the script thread.
- A simulator-changed handler runs after the delegation pass, with nothing
  held, and is given an id.
- What `SetOwner`, `SetStreamTarget` and the viewer filters do to interest
  lands on the next tick, not at the call.
- `GetPeerIdentity` and `Player::ResolveIdentity` return an optional copy.
- A typed RPC whose `Serialize` probes for optional trailing fields is
  dropped when they are absent: reading past the end is what "cut short"
  means.
- A handler or a packet-thread override that throws is logged, and the
  server carries on.
- A client's script events are rate-limited, and a client that floods is
  dropped.
- `ForEachStreamingPeer(id, fn)` is `StreamingPeers(id)`, which returns the
  peers.
- Registering an RPC handler or a world state after the packet thread has
  started aborts.
- A script wrapper of an entity is made without asking the world, for any
  id; only `new Entity(id)` written in a script refuses an unknown one, and
  a mod's handle class needs `Entity::ConstructFromScript<T>` to do the
  same.
- An entity created off the script thread belongs to no resource.
- The automatic replication worker count is one lower, and is zero on a
  host with three cores or fewer.
- `Instance::Shutdown` called from a packet handler does not shut down: it
  logs, and asks `Run()` to stop.

## A porting order that works

1. Change `GetInterestDependency` and anything else in `shared/` that used
   `ResolveSibling`. Client and server both build against it.
2. Move the client to `ClientEntities`.
3. On the server, list what packet handlers and the five `On*` overrides
   touch that is not an entity. Register it as state.
4. Port the `On*` overrides, then handlers feature by feature, then script
   bindings.
5. Port `PostUpdate` and services that walked entities by pointer to
   `world.ForEach` and handles.
6. Run every handler and every binding once. The two aborts described at
   the top are the errors this finds.
