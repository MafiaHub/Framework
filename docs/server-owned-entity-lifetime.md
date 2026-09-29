# Server-owned entity lifetime

`DelegationManager` hands an entity to the client best placed to simulate
it by making that client the entity's owner. Ownership also lets the owner
destroy the entity, which is right for an entity the client created and
wrong for one the server keeps -- a stack of items on the ground, say: its
simulator must be able to move it, never make it disappear.

Override `NetworkEntity::CanOwnerDestroy()` to return false for such an
entity:

```cpp
bool CanOwnerDestroy() const override {
    return false;
}
```

The server then refuses a destruction from the owner, as it already refuses
one from any other client. It adds a restriction, never a permission.
Clients still apply the server's destructions, and the server removes the
entity as usual with `ReplicationManager::DestroyEntity(entity)`.

Everything else a delegated entity needs is the delegation feature's: the
simulator writes the fields it owns, and every server write is followed by
`ForceState()` (see `delegation.h`).

## Verification

`builds\build.bat RunFrameworkTests 64`: the replication authority tests
cover an owner's destruction refused under the policy and the server's
destruction still applied on a client.
