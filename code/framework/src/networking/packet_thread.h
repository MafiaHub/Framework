/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2/function2.hpp>

#include <atomic>
#include <cstdint>
#include <thread>

namespace Framework::Networking {
    class NetworkPeer;

    // Server: the thread everything that arrives from the wire is handled on -- RPC handlers, inbound
    // entity state, connections opening and closing -- so that the tick never handles a packet.
    //
    // There is one, so handlers run one at a time, in arrival order. It takes no part in the handoff
    // of the world between the tick and the scripts (Utils::WorldLock): what a handler touches is
    // an entity or registered state reached by handle (replication/world.h), or guards itself.
    class PacketThread final {
      public:
        // How long the thread sleeps without a packet before it calls Receive() anyway: plugins
        // (timeouts, ReadyEvent, the build challenge) are only updated from inside it.
        static constexpr uint32_t kWaitMs = 100;

        PacketThread() = default;
        ~PacketThread();
        PacketThread(const PacketThread &)            = delete;
        PacketThread &operator=(const PacketThread &) = delete;

        // Starts draining `peer`, which must outlive the thread and be started. `onEntry` runs first,
        // on the thread: where it binds what is per thread, such as its scripting runtime.
        void Start(NetworkPeer &peer, fu2::unique_function<void()> onEntry = {});
        // Joins: no handler runs once this returns. The handler in progress finishes, and one more may
        // start if its packet was being handed over as the flag went up. RPCs still queued behind
        // it are dropped unhandled (one Receive() hands every RPC it finds to its handler, which
        // declines); other packets are left in the peer. The caller must not hold what a handler
        // may be waiting for.
        void Stop();

        bool OnPacketThread() const {
            return _threadId.load(std::memory_order_acquire) == std::this_thread::get_id();
        }

      private:
        void Loop();

        NetworkPeer *_peer = nullptr;
        fu2::unique_function<void()> _onEntry;
        std::thread _thread;
        std::atomic<std::thread::id> _threadId {};
        std::atomic<bool> _stop {false};
    };
} // namespace Framework::Networking
