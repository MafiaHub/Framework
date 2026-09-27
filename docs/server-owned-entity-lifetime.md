# Server-owned entity lifetime

An entity can delegate physics to a client while retaining server control
over its existence. Override `NetworkEntity::CanOwnerDestroy()` to return
false for durable world objects. The existing authenticated-owner check
still applies; this policy adds a restriction rather than granting access.
Clients continue accepting the server's destruction messages.

The base construction and state snapshots now include the server's virtual
world. Changes also push it directly to the current owner, which is excluded
from ordinary relays. It precedes game-specific forced fields, so an override
cannot accidentally omit it. Owner updates cannot change it on the server.
This changes the shared wire format and requires matching version 29 clients and servers.

Script handles can override the Entity position, rotation and virtual-world
setters. A durable object can therefore commit its placement before exposing
the new transform. The rotation property converts a rejected override into
a catchable JavaScript error, matching the other bound setter paths.

## Verification

Run `builds\build.bat RunFrameworkTests 64`. The replication authority tests
cover owner/non-owner destruction, server destruction on clients, world
seeding on construction, subsequent world updates, forced owner updates and forged owner worlds.

For an integrating mod, create a durable entity with a client physics owner.
Verify that an owner destruction request leaves it alive, a server removal
removes it from both clients, and a late joiner receives its current world.
Exercise the script placement overrides with valid and refused writes;
refusal must leave the durable placement unchanged and throw to the caller.

## API example

An existing registered entity type can keep its usual physics delegation
while refusing owner-authored deletion:

```cpp
bool CanOwnerDestroy() const override {
    return false;
}
```

`CanOwnerDestroy()` defaults to true. The server calls this policy only after
validating the sender's ownership. Returning true never authorizes another
client to delete the entity. Normal server destruction remains available
through `ReplicationManager::DestroyEntity(entity)`.

Server code changes the entity's world through the normal typed setter:

```cpp
entity->SetVirtualWorld(7);
```

This sends the current owner a forced snapshot when the value changes;
other observers receive ordinary state updates or streaming changes. The
world is also included in construction for late joiners. The framework's
`SerializeForcedSnapshot` places the world before the game's
`SerializeForcedState` extension, even if that override omits a base call.
Game implementations should keep overriding `SerializeForcedState` for
their own forced fields.

Durable script handles can override `Entity::SetPosition`,
`SetRotationFromEuler`, `SetRotationFromQuaternion` and `SetVirtualWorld`.
Validate and commit the durable placement before updating the replica.
Throwing `std::runtime_error` from a rejected setter reaches JavaScript as
a catchable error; do not change the replica before rejecting the write.
