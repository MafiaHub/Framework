# Packet Thread Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The server handles everything that arrives from the wire on a packet thread, and every entity access on every thread goes through a locked handle.

**Architecture:** A packet thread owned by `NetworkServer` drains `Receive()`; the tick thread keeps interest, delegation, state-bag flush and the replication pass. `Replication::World` hands out scope-bound handles that hold a per-entity (or per-registered-state) lock; raw `NetworkEntity *` leaves the server's public API. MafiaNet gains a blocking wait for packets and a ReplicaManager3 whose inbound path is safe against a pass on another thread.

**Tech Stack:** C++23 (MSVC, GCC 13, Apple clang subset), MafiaNet (RakPeer, RPC4, ReplicaManager3), V8/libnode, the repository's `unit.h` test macros, GoogleTest in MafiaNet.

**Spec:** `docs/superpowers/specs/2026-11-12-network-thread-design.md`

## Global Constraints

- One model. No opt-in, opt-out, legacy registration or flag.
- No handler API that posts to the tick thread by default.
- No raw `NetworkEntity *` reachable from mod code on the server.
- The client keeps its single thread; `NetworkPeer::RegisterRPC<T>(payload, packet)` stays for it.
- One packet thread: handlers run one at a time, in arrival order.
- Consistency is per entity, not per pass. Mutations are not staged to the pass.
- C++23 limited to what MSVC, GCC 13 and Apple clang share; no C++23 library types in SDK headers.
- Framework work stays on branch `server-multithreading`. Commit format `Module: Brief description`, wrapped at 72.
- MafiaNet work is a PR on `MafiaHub/MafiaNet` (conventional commits, as its history uses). The Framework builds against it with `FW_MAFIANET_ROOT` until a release is pinned.
- Build on macOS with `cmake --build build --target FrameworkTests`; run `build/bin/FrameworkTests` from a temporary cwd; `FW_TEST_FILTER=<module>` narrows it.
- Test macros `EQUALS` etc. `break` out of the `IT` block: use `break`, never `return`, and never inside a lambda.
- Whole-server test modules stay last in `code/tests/framework_ut.cpp`.
- Do not modify `~/dev/Mafia2Online/mod`.

## Review Focus

1. An entity destroyed by the tick or a script while a handler holds its handle: the handle stays valid until released; the id stops resolving at once. (Task 3)
2. A packet from a connection that dropped a moment earlier: `Sender::Avatar()` is 0, handles come back empty, nothing crashes. (Task 7)
3. Shutdown with packets in flight or a handler running: the packet thread is joined before anything it uses is freed; no handler starts after teardown begins. (Task 7)
4. The same id asked for twice in one multi-acquire (`Write<A, A>(x, x)`): the first handle is filled, the second is empty; no self-deadlock. (Task 3)
5. Hostile input on the packet thread (unknown RPC, truncated payload, control packet behind a timestamp): dropped as today, the thread survives and keeps draining. (Task 7)

---

## Part A: MafiaNet (`~/dev/mafiahub/MafiaNet`, branch `feat/receive-off-pass-thread`)

### Task 1: Blocking wait for inbound packets

**Files:**
- Modify: `Source/include/mafianet/peer.h`, `Source/include/mafianet/peerinterface.h`, `Source/src/peer.cpp` (the three `packetReturnQueue.Push*` sites near lines 2978 and 4876, and `Shutdown`)
- Test: `Tests/Integration/` (new file `wait_for_packets_test.cpp`, added to `Tests/CMakeLists.txt`)

**Interfaces:**
- Produces: `bool RakPeerInterface::WaitForPackets(MafiaNet::TimeMS timeoutMs)`: returns true when at least one packet is waiting in the return queue, false on timeout or once the peer is no longer active. Callable from any one thread.

- [ ] **Step 1: Failing tests** (`WaitForPackets` suite)
  - `ReturnsFalseAfterTimeoutWithNothingToReceive`: `WaitForPackets(50)` is false and took at least 40 ms.
  - `WakesWhenAPacketArrives`: a second peer connects and sends; a thread blocked in `WaitForPackets(5000)` returns true in under 1000 ms and `Receive()` then yields a packet.
  - `ReturnsAtOnceWhenAPacketIsAlreadyWaiting`.
  - `WakesOnShutdown`: a thread blocked in `WaitForPackets(5000)` returns false in under 1000 ms after `Shutdown(0)`.
- [ ] **Step 2: Run** `ctest --test-dir build -R WaitForPackets`; expect compile failure.
- [ ] **Step 3: Implement** with a condition variable beside `packetReturnMutex`, notified at every push and in `Shutdown`.
- [ ] **Step 4: Run**; expect 4 passed.
- [ ] **Step 5: Commit** `feat(peer): wait for inbound packets without polling`.

### Task 2: ReplicaManager3 inbound path safe against a pass on another thread

**Files:**
- Modify: `Source/include/mafianet/replica_manager3.h`, `Source/src/replica_manager3.cpp` (`OnReceive` at 768, `Update` at 995, connection add/remove hooks)
- Test: `Tests/Integration/rm3_receive_thread_test.cpp`

**Interfaces:**
- Produces: `ReplicaManager3::Update()` may run on one thread while `RakPeer::Receive()` (hence `ReplicaManager3::OnReceive`, `OnNewConnection`, `OnClosedConnection`) runs on another. Inbound state for connection X blocks only while X itself is being serialized or having its lists edited. No new public call is required of the owner.

- [ ] **Step 1: Failing test** `Rm3ReceiveThread.InboundStateArrivesWhileThePassRunsElsewhere`: server and client peers on loopback, 64 replicas whose owner client serializes a counter upstream; on the server one thread loops `Update()` through a subclass that gates the plugin `Update()` the way the Framework does, another loops `WaitForPackets(10)`/`Receive()`, for 2 s. Asserts every replica's counter on the server advanced, and the connection was closed and reopened once mid-run without a crash.
- [ ] **Step 2: Run** under the existing ThreadSanitizer tree (`build-tsan-issue7` is a precedent; configure a fresh `build-tsan` with `-fsanitize=thread`); expect data-race reports.
- [ ] **Step 3: Implement.** Per-connection mutex in `Connection_RM3`; `m_WorldListMutex` shortened to list edits and lookups so it is not held across a whole pass; `inUpdate` made safe for the two-thread case.
- [ ] **Step 4: Run** under TSan and without; expect pass, zero reports. Run the full MafiaNet suite.
- [ ] **Step 5: Commit** `feat(rm3): receive on one thread while the pass runs on another`. Push, open the PR on `MafiaHub/MafiaNet`, wait for CI.

---

## Part B: Framework (`server-multithreading`)

Configure the Framework against the MafiaNet branch: install MafiaNet Release into a temp prefix and pass `-DFW_MAFIANET_ROOT=<prefix>` when configuring `build/`.

### Task 3: `Replication::World` and its handles

**Files:**
- Create: `code/framework/src/networking/replication/world.h`, `world.cpp`
- Modify: `code/framework/src/networking/replication/replication_manager.{h,cpp}` (owns the `World`; the lock lives in the manager's per-entity slot, not in `NetworkEntity`)
- Test: `code/tests/modules/world_ut.h`, registered in `code/tests/framework_ut.cpp`

**Interfaces:**
- Produces, in `Framework::Networking::Replication`:
  - `template <typename T> class Ref` (write) and `ConstRef` (read): move-only, `explicit operator bool`, `operator->`, `operator*`. Not storable beyond scope by convention; no copy.
  - `class World`:
    - `template <typename T> ConstRef<T> Read(uint64_t id)`, `template <typename T> Ref<T> Write(uint64_t id)`; for a registered state type the id parameter is absent.
    - `template <typename... Ts> std::tuple<Ref<Ts>...> Write(Ids... ids)` and the `Read` form: one id per entity type in order, none for state types.
    - `template <typename T> void ForEach(fu2::function<void(Ref<T> &)> fn)`.
    - `template <typename T> Ref<T> Create()`; `bool Destroy(uint64_t id)`.
    - `template <typename T> void RegisterState(std::unique_ptr<T> state)`: before the server starts.
  - `ReplicationManager::GetWorld() -> World &`.
- Lock order: registered states by registration order, then entities by ascending network id.
- A second acquire while a handle is held on this thread, outside one multi-acquire call, logs critical and calls `std::abort()` in every build type. `World::HeldByThisThread() -> size_t` exposes the count.

- [ ] **Step 1: Failing tests** (`world` module)
  - "gives an empty handle for an id that does not resolve, and for the wrong type".
  - "reads and writes an entity through a handle".
  - "takes several entities at once whatever order they are asked in": two threads, 20 000 iterations each, one asks `(a, b)`, the other `(b, a)`; finishes within 10 s.
  - "fills the first handle and leaves the second empty when one id is asked for twice".
  - "keeps an entity alive for a handle held across its destruction, and stops resolving its id at once".
  - "visits every entity of a type one lock at a time": `HeldByThisThread() == 1` inside the callback.
  - "reaches registered state alone and together with an entity".
  - "counts handles held by the thread": 0, then 2 inside a multi-acquire scope, then 0.
- [ ] **Step 2: Run** `FW_TEST_FILTER=world`; expect compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run**; expect 8 passed. Full suite still passes.
- [ ] **Step 5: Commit** `Replication: Reach entities through locked handles`.

### Task 4: `Sender` and the server's RPC signature

**Files:**
- Create: `code/framework/src/networking/sender.h`
- Modify: `code/framework/src/networking/network_server.{h,cpp}`, `network_peer.h`
- Test: `code/tests/modules/network_packets_ut.h`

**Interfaces:**
- Consumes: `World` (Task 3).
- Produces:
  - `class Networking::Sender`: `MafiaNet::PeerGuid Guid() const`, `uint64_t Avatar() const` (0 when none), `template <RPC::Payload T> void Reply(T payload, Priority = High, Reliability = ReliableOrdered) const`, `void Drop(std::string_view reason) const`.
  - `template <RPC::Payload T> void NetworkServer::RegisterRPC(fu2::function<void(const T &, Sender, Replication::World &) const>)` and `RegisterRawRPC(RPC::Identifier, fu2::function<void(MafiaNet::BitStream *, Sender, Replication::World &)>)`. The inherited `(payload, packet)` forms are deleted on `NetworkServer`.

- [ ] **Step 1: Failing tests**: "hands a server handler the sender and the world" (loopback client sends an RPC; handler sees `sender.Guid()` equal to the client's guid and can `world.Write` its avatar); "replies to the sender only" (two clients; one receives).
- [ ] **Step 2: Run** `FW_TEST_FILTER=network_packets`; expect compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run**; expect pass.
- [ ] **Step 5: Commit** `Networking: Give server RPC handlers the sender and the world`.

### Task 5: Move the Framework's own code to handles; remove the raw accessors

**Files:**
- Modify: `code/framework/src/networking/replication/*` (13 files), `scripting/builtins/*` (4), `voice/server/*` (1), `world/types/*` (2), `integrations/server/instance.{h,cpp}`, `integrations/server/scripting/*`; `integrations/client/*` only for the `GetInterestDependency` change
- Test: existing modules; one addition in `interest_grid_ut.h`

**Interfaces:**
- Consumes: Tasks 3 and 4.
- Produces:
  - `ReplicationManager::GetEntityByNetworkID`, `ForEachEntity`, `GetViewer`, `ForEachViewer`, `ForEachAvatar`, `CreateEntity`, `DestroyEntity` are no longer public on the server; `World` and the manager's own code are the only callers. The client keeps a single-threaded accessor set (the builder names it; it must not compile into server targets' public headers).
  - `virtual uint64_t NetworkEntity::GetInterestDependency()` returns the dependency's network id, 0 for none. `ResolveSibling` is removed.
  - `SetOwner`, `ForceState`, `SetVirtualWorld`, `SetViewer`, `MarkStateBagDirty` take an id or a `Ref` and are safe from any thread.
  - The replication pass, interest rebuild, delegation and inbound `Deserialize` hold the entity's lock for the entity they touch.
  - `Instance::OnPlayerConnect(const PlayerConnectionData &, Replication::World &)`, `OnPlayerReady(guid, World &)`, `OnPlayerDisconnect(guid, World &)`, `OnChatMessage(senderNetworkId, text, World &)`, `OnChatCommand(..., World &)`, `OnClientEvent(...)` unchanged in arguments.
  - The Framework's server RPC handlers (`ClientJoin`, `ChatMessage`, `EmitScriptEvent`, `VoicePreference`, `VoiceTierRequest`, and the four in `replication_manager.cpp`) use the Task 4 signature.
  - `VoiceServer`'s public methods are safe from the packet thread and the tick thread.
- The tick thread still calls `Receive()` in this task. Behaviour is unchanged.

- [ ] **Step 1: Failing test** in `interest_grid_ut.h`: "resolves an interest dependency by id": an entity whose `GetInterestDependency()` returns another's id is shown only with it; an id that no longer resolves counts as no dependency.
- [ ] **Step 2: Migrate**, one directory per commit where the tree builds in between.
- [ ] **Step 3: Verify.** `grep -rn "GetEntityByNetworkID\|ForEachEntity\|ResolveSibling" code/framework/src --include=*.h` shows no public server declaration. Full suite passes; `FrameworkSdkSmoke` builds; `bash scripts/check_style.sh` is clean.
- [ ] **Step 4: Commit(s)** `Replication: ...`, `Scripting: ...`, `Server: ...`.

### Task 6: One argument-building runtime per non-script thread

**Files:**
- Modify: `code/framework/src/scripting/node_engine.{h,cpp}`, `integrations/server/instance.cpp`
- Test: `code/tests/modules/server_script_thread_ut.h`

**Interfaces:**
- Produces:
  - `bool NodeEngine::CreateOffThreadRuntime(std::string_view name)`: on the script thread, after `Init()`, once per name. The server creates `"tick"` and `"packet"`.
  - `void NodeEngine::BindOffThreadRuntime(std::string_view name)`: on the thread that will use it. A non-script thread that bound nothing sees `"tick"`.
  - `GetIsolate()`/`GetContext()` off the script thread name the calling thread's bound runtime.

- [ ] **Step 1: Failing test** "raises an event from a second thread while the tick thread is inside its own isolate": the tick thread holds a `v8::Locker` on `engine->GetIsolate()` for 500 ms; another thread, bound to `"packet"`, emits as projects do; the emit returns in under 100 ms and the handler's marker file appears.
- [ ] **Step 2: Run** `FW_TEST_FILTER=server_script_thread`; expect that test to fail (the emit waits about 500 ms).
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run**; expect pass, the other 12 still passing.
- [ ] **Step 5: Commit** `Scripting: Give each thread off the script thread its own runtime`.

### Task 7: The packet thread

**Files:**
- Create: `code/framework/src/networking/packet_thread.{h,cpp}`
- Modify: `networking/network_peer.{h,cpp}` (`Update()` loses the drain; the drain becomes `DrainPackets()`), `networking/network_server.{h,cpp}`, `integrations/server/instance.{h,cpp}` (`TickBreakdown`, the `Threads:` line, option `slowPacketHandlerWarnMs` default 10, shutdown order), `utils/thread_budget.*`
- Test: `code/tests/modules/server_packet_thread_ut.h`, registered among the whole-server modules at the end of `framework_ut.cpp`

**Interfaces:**
- Consumes: Tasks 1 to 6.
- Produces:
  - `class Networking::PacketThread`: `void Start(NetworkPeer &peer)`, `void Stop()` (joins), `bool OnPacketThread() const`. Loop: `WaitForPackets(100)`, then `peer.DrainPackets()`. Binds the `"packet"` runtime on entry.
  - `NetworkPeer::Update()` on the server: interest rebuild, delegation, state-bag flush, the pass. No `Receive()`.
  - Connect, disconnect, admission and the Framework's RPC handlers run on the packet thread. `EmitScriptEvent` is decoded there and handed straight to `PostToScripts`.
  - `EmitReservedSync` called with `World::HeldByThisThread() != 0` logs critical and aborts.
  - A handler longer than `slowPacketHandlerWarnMs` logs a rate-limited warning naming the RPC.
  - Shutdown: stop the packet thread before the script thread and before the peer is shut down.

- [ ] **Step 1: Failing tests** (`server_packet_thread` module, loopback client against a whole server)
  - "runs a handler off the tick thread".
  - "keeps ticking while a handler takes its time": a handler sleeping 300 ms; longest gap between ticks under 100 ms.
  - "runs one connection's handlers in the order they were sent": 1000 sequenced RPCs arrive 0..999.
  - "applies inbound state while the pass serializes": a client-owned entity's counter advances over 1 s with 200 other entities replicating; no crash.
  - "lets a handler raise an event with a handle held": the handler's marker appears; no hang (10 s limit).
  - "survives clients connecting and dropping in a storm": 50 connect/disconnect cycles; server still answers an RPC afterwards; viewer count is 0.
  - "gives a handler empty handles for a sender that has gone": RPC queued, client dropped before the drain; `sender.Avatar() == 0`. (Review Focus 2)
  - "drops hostile packets and keeps draining": an unknown RPC id, a truncated payload and a control id behind a timestamp, then a valid RPC that is handled. (Review Focus 5)
  - "shuts down while a handler is running": `Shutdown()` returns within 5 s and the handler's side effect is either complete or absent, never partial. (Review Focus 3)
- [ ] **Step 2: Run** `FW_TEST_FILTER=server_packet_thread`; expect "runs a handler off the tick thread" and "keeps ticking" to fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** the module 5 times, then the full suite; expect all passing.
- [ ] **Step 5: Commit** `Networking: Handle packets on a thread of their own`.

### Task 8: ThreadSanitizer job

**Files:**
- Modify: `CMakeLists.txt` (option `FW_SANITIZE`, values empty or `thread`), `.github/workflows/` (the test workflow gains a Linux job)

- [ ] **Step 1:** Add `FW_SANITIZE=thread` (compile and link `-fsanitize=thread`, Linux and macOS only).
- [ ] **Step 2:** CI job: build MafiaNet from source at the pinned ref with `-fsanitize=thread`, install to a prefix, configure the Framework with `-DFW_MAFIANET_ROOT=<prefix> -DFW_SANITIZE=thread`, run `FW_TEST_FILTER=world,fork_join_pool,task_scheduler,world_lock,replication_parallel,network_packets`. The V8 modules are left out: libnode is not instrumented.
- [ ] **Step 3:** Run the same locally on macOS; expect zero reports. Fix what it finds in the task that owns the code.
- [ ] **Step 4: Commit** `CI: Run the threading tests under ThreadSanitizer`.

### Task 9: Docs, migration guide, M2O proof, release plumbing

**Files:**
- Modify: `docs/server_threading.md`, `AGENTS.md`, `docs/project_architecture.md` (handler and entity-access rules), PR #398 body
- Create: `docs/migrating_to_the_packet_thread.md`
- Modify when the MafiaNet release exists: `cmake/MafiaNetPin.cmake`, `VERSION` (MAJOR; the owner confirms)

- [ ] **Step 1:** Rewrite the threading doc's thread table and "What still runs on the tick thread"; document handles, `Sender`, the lock-order and no-nested-acquire rules, per-entity consistency, sync emits and handles.
- [ ] **Step 2:** Migration guide with before/after for: an RPC handler, a script binding resolving an entity, mod state registered with the world, an `On*` override, `GetInterestDependency`.
- [ ] **Step 3:** Port M2O's server in the temp copy (`$T/src`), following the guide; build `M2OServer`; rerun the boot and `busy` comparisons. Record what the port needed that the guide did not say, and fix the guide.
- [ ] **Step 4:** Measure tick time under a packet flood before and after (loopback clients sending a handler-heavy RPC), and put the numbers in the PR body.
- [ ] **Step 5:** Commit, push, wait for CI on three platforms, handle CodeRabbit threads.
- [ ] **Step 6 (blocked on the owner):** MafiaNet release, pin bump, VERSION.
