/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#include "packet_thread.h"

#include "network_peer.h"

#include <logging/logger.h>
#include <utils/profiler.h>

#include <utility>

namespace Framework::Networking {
    PacketThread::~PacketThread() {
        Stop();
    }

    void PacketThread::Start(NetworkPeer &peer, fu2::unique_function<void()> onEntry) {
        if (_thread.joinable()) {
            return;
        }
        peer.OnPacketThreadStarting();
        _peer    = &peer;
        _onEntry = std::move(onEntry);
        _stop    = false;
        _thread  = std::thread([this] {
            _threadId.store(std::this_thread::get_id(), std::memory_order_release);
            FW_PROFILE_THREAD("Packets");
            if (_onEntry) {
                _onEntry();
            }
            Loop();
        });
    }

    void PacketThread::Stop() {
        if (!_thread.joinable()) {
            return;
        }
        if (OnPacketThread()) {
            // A thread cannot wait for itself: joining here would end the process in std::thread.
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->critical("PacketThread::Stop was called from a packet handler; the thread stops after this handler, and has to be joined from another thread");
            _stop.store(true, std::memory_order_release);
            return;
        }
        _stop.store(true, std::memory_order_release);
        _thread.join();
        _threadId.store(std::thread::id {}, std::memory_order_release);
        _onEntry = {};
    }

    void PacketThread::Loop() {
        MafiaNet::RakPeerInterface *peer = _peer->GetPeer();
        while (!_stop.load(std::memory_order_acquire) && peer->IsActive()) {
            // True or not: the wait can end for a peer that is shutting down, and a quiet interval
            // still owes the plugins their update.
            peer->WaitForPackets(kWaitMs);
            if (_stop.load(std::memory_order_acquire)) {
                break;
            }
            _peer->DrainPackets(&_stop);
        }
    }
} // namespace Framework::Networking
