# Networking API Migration Guide

This guide explains how to migrate from the previous networking API to the new fluent router-based API introduced in the `network-api-revamp` branch.

## Overview of Changes

The new API introduces several improvements:

1. **Constructor-based message initialization** instead of `FromParameters()` methods
2. **Fluent router API** for registering message handlers instead of `RegisterMessage()`/`RegisterRPC()` calls
3. **Handler classes** for organizing message receivers with proper dependency injection
4. **Removal of macro-based RPC sending** in favor of direct method calls

---

## 1. Message Construction

### Before: `FromParameters()` Method

```cpp
Framework::Networking::Messages::ClientHandshake msg;
msg.FromParameters(_opts.modVersion, Utils::Version::rel, _opts.gameVersion, _opts.gameName);
net->Send(msg, guid);
```

### After: Constructor Initialization

```cpp
Framework::Networking::Messages::ClientHandshake msg(
    _opts.modVersion, Utils::Version::rel, _opts.gameVersion, _opts.gameName);
net->Send(msg, guid);
```

### Migration Steps

1. Find all `FromParameters()` calls
2. Replace with constructor arguments
3. All message classes now have:
   - A default constructor: `MessageClass() = default;`
   - A parameterized constructor: `MessageClass(args...)`

### Examples

| Message Class | Old API | New API |
|--------------|---------|---------|
| `ClientHandshake` | `msg.FromParameters(version, fwVersion, gameVersion, gameName)` | `ClientHandshake(version, fwVersion, gameVersion, gameName)` |
| `ClientKick` | `msg.FromParameters(reason, customReason)` | `ClientKick(reason, customReason)` |
| `ClientConnectionFinalized` | `msg.FromParameters(tickRate, entityID)` | `ClientConnectionFinalized(tickRate, entityID)` |
| `ClientRequestStreamer` | `msg.FromParameters(name, steamId, discordId, hwId)` | `ClientRequestStreamer(name, steamId, discordId, hwId)` |
| `GameSyncEntitySpawn` | `msg.FromParameters(transform)` | `GameSyncEntitySpawn(transform)` |
| `GameSyncEntityUpdate` | `msg.FromParameters(transform, owner)` | `GameSyncEntityUpdate(transform, owner)` |
| `GameSyncEntityOwnerUpdate` | `msg.FromParameters(owner)` | `GameSyncEntityOwnerUpdate(owner)` |

---

## 2. Message Handler Registration

### Before: Lambda-Based Registration

```cpp
net->RegisterMessage<ClientHandshake>(
    Framework::Networking::Messages::GameMessages::GAME_CONNECTION_HANDSHAKE,
    [this, net](SLNet::RakNetGUID guid, ClientHandshake *msg) {
        // Handle message...
    });

net->RegisterRPC<Shared::RPC::EmitLuaEvent>(
    [this](SLNet::RakNetGUID guid, Shared::RPC::EmitLuaEvent *rpc) {
        // Handle RPC...
    });

net->RegisterGameRPC<Framework::World::RPC::SetTransform>(
    [this](SLNet::RakNetGUID guid, Framework::World::RPC::SetTransform *msg) {
        // Handle game RPC...
    });
```

### After: Fluent Router API

```cpp
auto r = net->router();

// Standard messages
r.on<ClientHandshake>().handle(this, &Instance::OnClientHandshake);

// RPCs
r.onRPC<Shared::RPC::EmitLuaEvent>().handle(this, &Instance::OnEmitLuaEvent);

// Game RPCs
r.onGameRPC<Framework::World::RPC::SetTransform>().handle(this, &Instance::OnSetTransform);
```

### Migration Steps

1. Get a router from the network peer: `auto r = net->router();`
2. Convert lambda handlers to instance methods
3. Register handlers using the fluent API:
   - `r.on<MessageType>().handle(instance, &Class::Method)` for messages
   - `r.onRPC<RPCType>().handle(instance, &Class::Method)` for RPCs
   - `r.onGameRPC<GameRPCType>().handle(instance, &Class::Method)` for game RPCs

### Handler Method Signature

All handler methods must have the signature:

```cpp
void OnMessageName(SLNet::RakNetGUID guid, MessageType *msg);
```

**Important:** The handler instance must outlive the `NetworkPeer`. Store handler objects as member variables.

---

## 3. Handler Classes for World Module Receivers

### Before: Inline Lambdas in Setup Functions

```cpp
void Base::SetupServerReceivers(
    Framework::Networking::NetworkPeer *net,
    Framework::World::Engine *worldEngine) {

    net->RegisterMessage<GameSyncEntityUpdate>(
        GameMessages::GAME_SYNC_ENTITY_UPDATE,
        [worldEngine](SLNet::RakNetGUID guid, GameSyncEntityUpdate *msg) {
            // Handle update...
        });
}
```

### After: Handler Classes

```cpp
// In header (base.hpp)
class ServerReceiverHandler {
    Engine *_worldEngine;

  public:
    explicit ServerReceiverHandler(Engine *worldEngine)
        : _worldEngine(worldEngine) {}

    void OnEntityUpdate(SLNet::RakNetGUID guid,
        Framework::Networking::Messages::GameSyncEntityUpdate *msg);
};

// In implementation (modules_impl.cpp)
void ServerReceiverHandler::OnEntityUpdate(
    SLNet::RakNetGUID guid,
    Framework::Networking::Messages::GameSyncEntityUpdate *msg) {

    if (!msg->Valid()) return;
    // Handle update...
}

void Base::SetupServerReceivers(
    Framework::Networking::NetworkPeer *net,
    ServerReceiverHandler *handler) {

    auto r = net->router();
    r.on<GameSyncEntityUpdate>().handle(handler, &ServerReceiverHandler::OnEntityUpdate);
}
```

### Handler Classes Available

| Class | Purpose |
|-------|---------|
| `ServerReceiverHandler` | Server-side entity update handling |
| `ClientReceiverHandler` | Client-side entity spawn/despawn/update handling |

### Integration Example

```cpp
// In your instance class header
std::unique_ptr<World::Modules::ServerReceiverHandler> _serverReceiverHandler;

// In InitNetworkingMessages()
_serverReceiverHandler = std::make_unique<World::Modules::ServerReceiverHandler>(
    _worldEngine.get());
Framework::World::Modules::Base::SetupServerReceivers(net, _serverReceiverHandler.get());
```

---

## 4. Removed Macros

The following macros have been removed:

| Macro | Replacement |
|-------|-------------|
| `FW_SEND_COMPONENT_RPC(rpc, ...)` | Direct method calls with constructor |
| `FW_SEND_COMPONENT_RPC_TO(rpc, guid, ...)` | Direct method calls with constructor |
| `FW_SEND_CLIENT_COMPONENT_GAME_RPC(rpc, ent, ...)` | Direct method calls |
| `FW_SEND_CLIENT_COMPONENT_GAME_RPC_TO(rpc, ent, guid, ...)` | Direct method calls |
| `FW_SEND_SERVER_COMPONENT_GAME_RPC(rpc, ent, ...)` | Direct method calls |
| `FW_SEND_SERVER_COMPONENT_GAME_RPC_EXCEPT(rpc, ent, guid, ...)` | Direct method calls |
| `FW_SEND_SERVER_COMPONENT_GAME_RPC_TO(rpc, ent, guid, ...)` | Direct method calls |

### Before: Macro Usage

```cpp
FW_SEND_COMPONENT_RPC(Framework::World::RPC::SetTransform, transform);
```

### After: Direct API

```cpp
auto net = Framework::CoreModules::GetNetworkPeer();
if (net) {
    Framework::World::RPC::SetTransform rpc(transform);
    net->SendRPC(rpc);
}
```

Or using the new templated helper:

```cpp
net->sendRPC<Framework::World::RPC::SetTransform>(guid, transform);
```

---

## 5. New Helper Methods

The `NetworkPeer` class now provides templated helper methods:

```cpp
// Send a message with constructor arguments
net->send<ClientKick>(guid, DisconnectionReason::KICKED, "Custom reason");

// Send an RPC with constructor arguments
net->sendRPC<EmitLuaEvent>(guid, eventName, payload);
```

---

## 6. RPC Class Changes

RPC classes now use constructors instead of `FromParameters()`:

### Before

```cpp
class SetTransform : public IRPC {
    World::Modules::Base::Transform _transform;
  public:
    void FromParameters(const World::Modules::Base::Transform &tr) {
        _transform = tr;
    }
    // ...
};
```

### After

```cpp
class SetTransform : public IRPC {
    World::Modules::Base::Transform _transform;
  public:
    SetTransform() = default;

    SetTransform(const World::Modules::Base::Transform &tr)
        : _transform(tr) {}
    // ...
};
```

---

## Migration Checklist

- [ ] Update all message construction to use constructors instead of `FromParameters()`
- [ ] Convert lambda-based handlers to instance methods
- [ ] Update handler registration to use the fluent router API
- [ ] Create handler classes for complex receiver setups
- [ ] Store handler instances as member variables (ensure lifetime >= NetworkPeer)
- [ ] Replace removed macros with direct API calls
- [ ] Add forward declarations for message types in headers
- [ ] Update any custom message/RPC classes to use constructor initialization

---

## Example: Complete Server Instance Migration

### Before

```cpp
void Instance::InitNetworkingMessages() const {
    using namespace Framework::Networking::Messages;
    const auto net = _networkingEngine->GetNetworkServer();

    net->RegisterMessage<ClientHandshake>(
        GameMessages::GAME_CONNECTION_HANDSHAKE,
        [this, net](SLNet::RakNetGUID guid, ClientHandshake *msg) {
            if (!msg->Valid()) {
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }
            // Validate and respond...
            ClientReadyAssets readyMsg;
            net->Send(readyMsg, guid);
        });

    Framework::World::Modules::Base::SetupServerReceivers(net, _worldEngine.get());
}
```

### After

```cpp
// In header
class Instance {
    std::unique_ptr<World::Modules::ServerReceiverHandler> _serverReceiverHandler;

    void OnClientHandshake(SLNet::RakNetGUID guid,
        Framework::Networking::Messages::ClientHandshake *msg);
};

// In implementation
void Instance::InitNetworkingMessages() {
    const auto net = _networkingEngine->GetNetworkServer();
    auto r = net->router();

    r.on<Framework::Networking::Messages::ClientHandshake>()
        .handle(this, &Instance::OnClientHandshake);

    _serverReceiverHandler = std::make_unique<World::Modules::ServerReceiverHandler>(
        _worldEngine.get());
    Framework::World::Modules::Base::SetupServerReceivers(
        net, _serverReceiverHandler.get());
}

void Instance::OnClientHandshake(
    SLNet::RakNetGUID guid,
    Framework::Networking::Messages::ClientHandshake *msg) {

    const auto net = _networkingEngine->GetNetworkServer();

    if (!msg->Valid()) {
        net->GetPeer()->CloseConnection(guid, true);
        return;
    }
    // Validate and respond...
    ClientReadyAssets readyMsg;
    net->Send(readyMsg, guid);
}
```

---

## Breaking Changes Summary

1. `FromParameters()` methods removed from all message classes
2. `RegisterMessage()` no longer takes message ID - it's derived from the message type
3. Lambda handlers should be converted to instance methods for better organization
4. Receiver setup functions now require handler class instances
5. All `FW_SEND_*` macros have been removed
6. `InitNetworkingMessages()` is no longer `const` (handler storage required)
