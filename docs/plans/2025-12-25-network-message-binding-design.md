# Network Message Binding Redesign

## Problem

The current network message binding API is verbose and scattered:

- Registration requires lambdas with manual captures
- Message IDs passed separately from message types
- Macros like `FW_SEND_SERVER_COMPONENT_GAME_RPC` hide ugly casts
- `FromParameters()` pattern requires two-step construction
- Bindings spread across `InitNetworkingMessages()` with no clear structure

## Solution

A fluent router API for registration and clean template methods for sending.

## Design

### Registration API

All bindings in one place using a fluent router:

```cpp
void Instance::InitNetworkingMessages() {
    auto r = net->router();

    // Messages
    r.on<ClientHandshake>().handle(this, &Instance::OnClientHandshake);
    r.on<ClientRequestStreamer>().handle(this, &Instance::OnRequestStreamer);
    r.on<ClientInitPlayer>().handle(this, &Instance::OnInitPlayer);

    // RPCs
    r.onRPC<EmitLuaEvent>().handle(this, &Instance::OnEmitLuaEvent);

    // Game RPCs (entity-bound)
    r.onGameRPC<SetTransform>().handle(this, &Instance::OnSetTransform);
}
```

### Sending API

Template methods replace macros:

```cpp
// Messages
net->send<ClientHandshake>(guid, version, fwVer, gameVer, name);

// Regular RPCs
net->sendRPC<EmitLuaEvent>(guid, eventName, payload);

// Game RPCs - Server
net->sendGameRPC<SetTransform>(world, entity, transform);
net->sendGameRPCTo<SetTransform>(world, guid, entity, transform);
net->sendGameRPCExcept<SetTransform>(world, exceptGuid, entity, transform);

// Game RPCs - Client
net->sendGameRPC<SetTransform>(entity, transform);
```

### Message Class Changes

Replace `FromParameters()` with constructors:

```cpp
// Before
class ClientHandshake final : public IMessage {
public:
    void FromParameters(const std::string& clientVersion, ...);
};

// After
class ClientHandshake final : public IMessage {
public:
    ClientHandshake() = default;
    ClientHandshake(std::string clientVersion, std::string fwVersion,
                    std::string gameVersion, std::string gameName);
};
```

## Implementation

### NetworkPeer additions

```cpp
// network_peer.h

template <typename T>
class MessageBinder {
    NetworkPeer* _peer;
public:
    explicit MessageBinder(NetworkPeer* peer) : _peer(peer) {}

    template <typename Instance>
    void handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*)) {
        _peer->RegisterMessage<T>(T{}.GetMessageID(),
            [inst, method](SLNet::RakNetGUID guid, T* msg) {
                (inst->*method)(guid, msg);
            });
    }
};

template <typename T>
class RPCBinder {
    NetworkPeer* _peer;
public:
    explicit RPCBinder(NetworkPeer* peer) : _peer(peer) {}

    template <typename Instance>
    void handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*)) {
        _peer->RegisterRPC<T>(
            [inst, method](SLNet::RakNetGUID guid, T* rpc) {
                (inst->*method)(guid, rpc);
            });
    }
};

template <typename T>
class GameRPCBinder {
    NetworkPeer* _peer;
public:
    explicit GameRPCBinder(NetworkPeer* peer) : _peer(peer) {}

    template <typename Instance>
    void handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*)) {
        _peer->RegisterGameRPC<T>(
            [inst, method](SLNet::RakNetGUID guid, T* rpc) {
                (inst->*method)(guid, rpc);
            });
    }
};

class MessageRouter {
    NetworkPeer* _peer;
public:
    explicit MessageRouter(NetworkPeer* peer) : _peer(peer) {}

    template <typename T> MessageBinder<T> on() { return MessageBinder<T>(_peer); }
    template <typename T> RPCBinder<T> onRPC() { return RPCBinder<T>(_peer); }
    template <typename T> GameRPCBinder<T> onGameRPC() { return GameRPCBinder<T>(_peer); }
};

class NetworkPeer {
public:
    // ... existing code ...

    MessageRouter router() { return MessageRouter(this); }

    // New send methods
    template <typename T, typename... Args>
    bool send(SLNet::RakNetGUID guid, Args&&... args) {
        T msg(std::forward<Args>(args)...);
        return Send(msg, guid);
    }

    template <typename T, typename... Args>
    bool sendRPC(SLNet::RakNetGUID guid, Args&&... args) {
        T rpc(std::forward<Args>(args)...);
        return SendRPC(rpc, guid);
    }
};
```

### NetworkServer additions

```cpp
// network_server.h

class NetworkServer : public NetworkPeer {
public:
    // ... existing code ...

    template <typename T, typename... Args>
    bool sendGameRPC(World::ServerEngine* world, flecs::entity ent, Args&&... args) {
        T rpc(std::forward<Args>(args)...);
        rpc.SetServerID(ent.id());
        return SendGameRPC(world, rpc);
    }

    template <typename T, typename... Args>
    bool sendGameRPCTo(World::ServerEngine* world, SLNet::RakNetGUID guid,
                       flecs::entity ent, Args&&... args) {
        T rpc(std::forward<Args>(args)...);
        rpc.SetServerID(ent.id());
        return SendGameRPC(world, rpc, guid);
    }

    template <typename T, typename... Args>
    bool sendGameRPCExcept(World::ServerEngine* world, SLNet::RakNetGUID exceptGuid,
                           flecs::entity ent, Args&&... args) {
        T rpc(std::forward<Args>(args)...);
        rpc.SetServerID(ent.id());
        return SendGameRPC(world, rpc, SLNet::UNASSIGNED_RAKNET_GUID, exceptGuid);
    }
};
```

### NetworkClient additions

```cpp
// network_client.h

class NetworkClient : public NetworkPeer {
public:
    // ... existing code ...

    template <typename T, typename... Args>
    bool sendGameRPC(flecs::entity ent, Args&&... args) {
        T rpc(std::forward<Args>(args)...);
        rpc.SetServerID(ent.id());
        return SendGameRPC(rpc);
    }
};
```

## Removals

Once migration is complete, remove:

1. `FW_SEND_SERVER_COMPONENT_GAME_RPC` macro
2. `FW_SEND_SERVER_COMPONENT_GAME_RPC_TO` macro
3. `FW_SEND_SERVER_COMPONENT_GAME_RPC_EXCEPT` macro
4. `FromParameters()` methods from all message classes
5. Old `RegisterMessage(uint8_t, callback)` overload (keep template version internally)

## Files to Modify

1. `code/framework/src/networking/network_peer.h` - Add router, binders, send methods
2. `code/framework/src/networking/network_server.h` - Add server send methods
3. `code/framework/src/networking/network_client.h` - Add client send methods
4. `code/framework/src/integrations/client/instance.cpp` - Migrate to router API
5. `code/framework/src/integrations/server/instance.cpp` - Migrate to router API
6. `code/framework/src/world/server.h` - Remove macros
7. All message classes - Add constructors, remove `FromParameters()`
