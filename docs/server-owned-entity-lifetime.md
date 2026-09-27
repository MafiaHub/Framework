# Server-owned entity lifetime

An entity can delegate physics to a client while retaining server control
over its existence. Override `NetworkEntity::CanOwnerDestroy()` to return
false for durable world objects. The existing authenticated-owner check
still applies; this policy adds a restriction rather than granting access.
Clients continue accepting the server's destruction messages.

The base construction and state snapshots now include the server's virtual
world. Owner updates cannot change that value on the server. This changes
the shared wire format and requires matching version 29 clients and servers.

Script handles can override the Entity position, rotation and virtual-world
setters. A durable object can therefore commit its placement before exposing
the new transform. The rotation property converts a rejected override into
a catchable JavaScript error, matching the other bound setter paths.

## Verification

Run `builds\build.bat RunFrameworkTests 64`. The replication authority tests
cover owner/non-owner destruction, server destruction on clients, world
seeding on construction, subsequent world updates and forged owner worlds.

For an integrating mod, create a durable entity with a client physics owner.
Verify that an owner destruction request leaves it alive, a server removal
removes it from both clients, and a late joiner receives its current world.
Exercise the script placement overrides with valid and refused writes;
refusal must leave the durable placement unchanged and throw to the caller.
