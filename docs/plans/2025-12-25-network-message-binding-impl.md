# Network Message Binding Redesign - Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace verbose lambda-based message registration with a fluent router API and clean send methods.

**Architecture:** Add a `MessageRouter` class with fluent binders that wrap existing registration. Add template send methods to NetworkPeer/NetworkServer/NetworkClient. Update message classes to use constructors instead of `FromParameters()`.

**Tech Stack:** C++17 templates, SLNet (SlikeNet), fu2::function

---

## Task 1: Add Router Infrastructure to NetworkPeer

**Files:**
- Modify: `code/framework/src/networking/network_peer.h`

**Step 1: Add forward declarations and binder classes before NetworkPeer class**

Add after line 22 (after the includes, before namespace):

```cpp
namespace Framework::Networking {
    // Forward declaration
    class NetworkPeer;

    // Binder for IMessage types
    template <typename T>
    class MessageBinder {
        NetworkPeer* _peer;
    public:
        explicit MessageBinder(NetworkPeer* peer) : _peer(peer) {}

        template <typename Instance>
        void handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*));
    };

    // Binder for IRPC types
    template <typename T>
    class RPCBinder {
        NetworkPeer* _peer;
    public:
        explicit RPCBinder(NetworkPeer* peer) : _peer(peer) {}

        template <typename Instance>
        void handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*));
    };

    // Binder for IGameRPC types
    template <typename T>
    class GameRPCBinder {
        NetworkPeer* _peer;
    public:
        explicit GameRPCBinder(NetworkPeer* peer) : _peer(peer) {}

        template <typename Instance>
        void handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*));
    };

    // Router - fluent API for message binding
    class MessageRouter {
        NetworkPeer* _peer;
    public:
        explicit MessageRouter(NetworkPeer* peer) : _peer(peer) {}

        template <typename T>
        MessageBinder<T> on() { return MessageBinder<T>(_peer); }

        template <typename T>
        RPCBinder<T> onRPC() { return RPCBinder<T>(_peer); }

        template <typename T>
        GameRPCBinder<T> onGameRPC() { return GameRPCBinder<T>(_peer); }
    };
```

**Step 2: Add router() method and new send methods to NetworkPeer class**

Add inside NetworkPeer class (after line 37, after the existing Send methods):

```cpp
        // Fluent router API
        MessageRouter router() { return MessageRouter(this); }

        // New send methods with constructor forwarding
        template <typename T, typename... Args>
        bool send(SLNet::RakNetGUID guid, Args&&... args) {
            T msg(std::forward<Args>(args)...);
            return Send(msg, guid);
        }

        template <typename T, typename... Args>
        bool send(uint64_t guid, Args&&... args) {
            T msg(std::forward<Args>(args)...);
            return Send(msg, guid);
        }

        template <typename T, typename... Args>
        bool sendRPC(SLNet::RakNetGUID guid, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            return SendRPC(rpc, guid);
        }
```

**Step 3: Add binder handle() implementations after NetworkPeer class**

Add before the closing namespace brace:

```cpp
    // MessageBinder implementation
    template <typename T>
    template <typename Instance>
    void MessageBinder<T>::handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*)) {
        T tmp{};
        _peer->RegisterMessage<T>(tmp.GetMessageID(),
            [inst, method](SLNet::RakNetGUID guid, T* msg) {
                (inst->*method)(guid, msg);
            });
    }

    // RPCBinder implementation
    template <typename T>
    template <typename Instance>
    void RPCBinder<T>::handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*)) {
        _peer->RegisterRPC<T>(
            [inst, method](SLNet::RakNetGUID guid, T* rpc) {
                (inst->*method)(guid, rpc);
            });
    }

    // GameRPCBinder implementation
    template <typename T>
    template <typename Instance>
    void GameRPCBinder<T>::handle(Instance* inst, void (Instance::*method)(SLNet::RakNetGUID, T*)) {
        _peer->RegisterGameRPC<T>(
            [inst, method](SLNet::RakNetGUID guid, T* rpc) {
                (inst->*method)(guid, rpc);
            });
    }
```

**Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 5: Commit**

```bash
git add code/framework/src/networking/network_peer.h
git commit -m "Networking: Add fluent router API to NetworkPeer"
```

---

## Task 2: Add Server-Specific Send Methods

**Files:**
- Modify: `code/framework/src/networking/network_server.h`

**Step 1: Add new sendGameRPC methods to NetworkServer**

Add inside NetworkServer class (after line 53, after existing SendGameRPC):

```cpp
        // New fluent send methods
        template <typename T, typename... Args>
        bool sendGameRPC(Framework::World::ServerEngine* world, flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(world, rpc);
        }

        template <typename T, typename... Args>
        bool sendGameRPCTo(Framework::World::ServerEngine* world, SLNet::RakNetGUID guid,
                          flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(world, rpc, guid);
        }

        template <typename T, typename... Args>
        bool sendGameRPCExcept(Framework::World::ServerEngine* world, SLNet::RakNetGUID exceptGuid,
                              flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(world, rpc, SLNet::UNASSIGNED_RAKNET_GUID, exceptGuid);
        }
```

**Step 2: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 3: Commit**

```bash
git add code/framework/src/networking/network_server.h
git commit -m "Networking: Add fluent sendGameRPC methods to NetworkServer"
```

---

## Task 3: Add Client-Specific Send Methods

**Files:**
- Modify: `code/framework/src/networking/network_client.h`

**Step 1: Add new sendGameRPC method to NetworkClient**

Add inside NetworkClient class (after line 92, after existing SendGameRPC):

```cpp
        // New fluent send method
        template <typename T, typename... Args>
        bool sendGameRPC(flecs::entity ent, Args&&... args) {
            T rpc(std::forward<Args>(args)...);
            rpc.SetServerID(ent.id());
            return SendGameRPC(rpc);
        }
```

**Step 2: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 3: Commit**

```bash
git add code/framework/src/networking/network_client.h
git commit -m "Networking: Add fluent sendGameRPC method to NetworkClient"
```

---

## Task 4: Add Constructors to Message Classes

**Files:**
- Modify: `code/framework/src/networking/messages/client_handshake.h`
- Modify: `code/framework/src/networking/messages/client_connection_finalized.h`
- Modify: `code/framework/src/networking/messages/client_kick.h`
- Modify: `code/framework/src/networking/messages/client_ready_assets.h`
- Modify: `code/framework/src/networking/messages/client_request_streamer.h`
- Modify: `code/framework/src/networking/messages/client_initialise_player.h`

**Step 1: Add constructor to ClientHandshake**

In `client_handshake.h`, add after line 23 (after `public:`):

```cpp
        ClientHandshake() = default;

        ClientHandshake(const std::string& clientVersion, const std::string& fwVersion,
                       const std::string& gameVersion, const std::string& gameName)
            : _clientVersion(clientVersion.c_str())
            , _fwVersion(fwVersion.c_str())
            , _gameVersion(gameVersion.c_str())
            , _gameName(gameName.c_str()) {}
```

**Step 2: Read and add constructor to ClientConnectionFinalized**

Read the file first to understand its fields, then add appropriate constructor.

**Step 3: Read and add constructor to ClientKick**

Read the file first to understand its fields, then add appropriate constructor.

**Step 4: Read and add constructor to ClientReadyAssets**

Read the file first to understand its fields, then add appropriate constructor.

**Step 5: Read and add constructor to ClientRequestStreamer**

Read the file first to understand its fields, then add appropriate constructor.

**Step 6: Read and add constructor to ClientInitPlayer**

Read the file first to understand its fields, then add appropriate constructor.

**Step 7: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 8: Commit**

```bash
git add code/framework/src/networking/messages/
git commit -m "Networking: Add constructors to message classes"
```

---

## Task 5: Add Constructors to GameSync Message Classes

**Files:**
- Modify: `code/framework/src/networking/messages/game_sync/entity_spawn.h`
- Modify: `code/framework/src/networking/messages/game_sync/entity_despawn.h`
- Modify: `code/framework/src/networking/messages/game_sync/entity_update.h`
- Modify: `code/framework/src/networking/messages/game_sync/entity_self_update.h`
- Modify: `code/framework/src/networking/messages/game_sync/entity_owner_update.h`

**Step 1: Read each file and add appropriate constructor**

For each file, read to understand fields, then add a constructor that initializes all fields.

**Step 2: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 3: Commit**

```bash
git add code/framework/src/networking/messages/game_sync/
git commit -m "Networking: Add constructors to GameSync message classes"
```

---

## Task 6: Add Constructors to RPC Classes

**Files:**
- Modify: `code/framework/src/world/game_rpc/set_transform.h`
- Modify: `code/framework/src/world/game_rpc/set_frame.h`
- Modify: `code/framework/src/integrations/shared/rpc/emit_lua_event.h`

**Step 1: Add constructor to SetTransform**

Read the file, then add constructor matching FromParameters signature.

**Step 2: Add constructor to SetFrame**

Read the file, then add constructor matching FromParameters signature.

**Step 3: Add constructor to EmitLuaEvent**

In `emit_lua_event.h`, add after line 21 (after `public:`):

```cpp
        EmitLuaEvent() = default;

        EmitLuaEvent(const std::string& name, const std::string& payload)
            : _eventName(name.c_str())
            , _payload(payload.c_str()) {}
```

**Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 5: Commit**

```bash
git add code/framework/src/world/game_rpc/
git add code/framework/src/integrations/shared/rpc/
git commit -m "Networking: Add constructors to RPC classes"
```

---

## Task 7: Migrate Server Instance to Router API

**Files:**
- Modify: `code/framework/src/integrations/server/instance.cpp`

**Step 1: Read current InitNetworkingMessages implementation**

Understand all current registrations.

**Step 2: Add handler methods to Instance class header**

In `instance.h`, add private handler methods for each message type.

**Step 3: Rewrite InitNetworkingMessages using router**

Replace lambda registrations with:

```cpp
void Instance::InitNetworkingMessages() const {
    const auto net = _networkingEngine->GetNetworkServer();
    auto r = net->router();

    r.on<ClientHandshake>().handle(this, &Instance::OnClientHandshake);
    r.on<ClientRequestStreamer>().handle(this, &Instance::OnClientRequestStreamer);
    r.on<ClientInitPlayer>().handle(this, &Instance::OnClientInitPlayer);
    r.onRPC<Shared::RPC::EmitLuaEvent>().handle(this, &Instance::OnEmitLuaEvent);

    // Connection callbacks remain as-is (not message handlers)
    net->SetOnPlayerDisconnectCallback(...);

    // World module receivers
    Framework::World::Modules::Base::SetupServerReceivers(net, _worldEngine.get());
}
```

**Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 5: Run tests**

Run: `cmake --build build --target RunFrameworkTests`
Expected: All tests pass

**Step 6: Commit**

```bash
git add code/framework/src/integrations/server/
git commit -m "Server: Migrate to fluent router API"
```

---

## Task 8: Migrate Client Instance to Router API

**Files:**
- Modify: `code/framework/src/integrations/client/instance.cpp`
- Modify: `code/framework/src/integrations/client/instance.h`

**Step 1: Read current InitNetworkingMessages implementation**

Understand all current registrations.

**Step 2: Add handler methods to Instance class header**

In `instance.h`, add private handler methods for each message type.

**Step 3: Rewrite InitNetworkingMessages using router**

Replace lambda registrations with router calls.

**Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 5: Run tests**

Run: `cmake --build build --target RunFrameworkTests`
Expected: All tests pass

**Step 6: Commit**

```bash
git add code/framework/src/integrations/client/
git commit -m "Client: Migrate to fluent router API"
```

---

## Task 9: Migrate World Module Receivers to Router API

**Files:**
- Modify: `code/framework/src/world/modules/modules_impl.cpp`

**Step 1: Read current SetupServerReceivers and SetupClientReceivers**

Understand current implementation.

**Step 2: Refactor to use router API**

Change function signatures to accept router or use router internally.

**Step 3: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 4: Run tests**

Run: `cmake --build build --target RunFrameworkTests`
Expected: All tests pass

**Step 5: Commit**

```bash
git add code/framework/src/world/modules/
git commit -m "World: Migrate module receivers to router API"
```

---

## Task 10: Remove Old Macros

**Files:**
- Modify: `code/framework/src/world/server.h`

**Step 1: Search for macro usage**

Run: `grep -r "FW_SEND_SERVER_COMPONENT_GAME_RPC" code/`

**Step 2: Replace any remaining macro usages with new API**

For each usage found, replace with `net->sendGameRPC<T>(...)`.

**Step 3: Delete macro definitions**

Remove lines 22-53 from `world/server.h` (the three macro definitions).

**Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 5: Run tests**

Run: `cmake --build build --target RunFrameworkTests`
Expected: All tests pass

**Step 6: Commit**

```bash
git add -A
git commit -m "Networking: Remove deprecated FW_SEND_SERVER_COMPONENT_GAME_RPC macros"
```

---

## Task 11: Remove FromParameters Methods

**Files:**
- All message and RPC classes modified in Tasks 4-6

**Step 1: Search for FromParameters usage**

Run: `grep -r "FromParameters" code/`

**Step 2: Replace any remaining usages with constructor calls**

Update any code still using `FromParameters()`.

**Step 3: Remove FromParameters methods from all classes**

Delete the `FromParameters()` method from each message/RPC class.

**Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Build succeeds with no errors

**Step 5: Run tests**

Run: `cmake --build build --target RunFrameworkTests`
Expected: All tests pass

**Step 6: Commit**

```bash
git add -A
git commit -m "Networking: Remove deprecated FromParameters methods"
```

---

## Task 12: Final Verification and Cleanup

**Step 1: Run full build**

Run: `cmake -B build && cmake --build build`
Expected: Clean build with no warnings related to changes

**Step 2: Run all tests**

Run: `cmake --build build --target RunFrameworkTests`
Expected: All tests pass

**Step 3: Run format script**

Run: `./scripts/format_codebase.sh`

**Step 4: Final commit if formatting changed anything**

```bash
git add -A
git commit -m "Style: Apply formatting"
```
