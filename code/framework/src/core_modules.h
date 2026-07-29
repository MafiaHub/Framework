/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cassert>

#include <scripting/module.h>

namespace Framework::Networking {
    class NetworkPeer;
} // namespace Framework::Networking

namespace Framework::Networking::Replication {
    class ReplicationManager;
} // namespace Framework::Networking::Replication


namespace Framework::Voice {
    class VoiceServer;
} // namespace Framework::Voice

namespace Framework::GUI {
    class Manager;
} // namespace Framework::GUI

namespace Framework::Input {
    class IInput;
} // namespace Framework::Input

namespace Framework::Integrations::Client {
    class Instance;
} // namespace Framework::Integrations::Client

namespace Framework {

#define FW_ASSERT_MODULE_REGISTRATION(current, incoming, name)                \
    do {                                                                       \
        if ((current) != nullptr && (incoming) != nullptr) {                   \
            assert(false && name " is already registered in CoreModules");     \
        }                                                                      \
    } while (0)

    // Process-global module registry; not thread-safe (set at init, read on the main loop).
    class CoreModules final {
      public:
        static void Reset() noexcept {
            _networkPeer      = nullptr;
            _replication      = nullptr;
            _voiceServer      = nullptr;
            _scriptingModule  = nullptr;
            _webManager       = nullptr;
            _input            = nullptr;
            _clientInstance   = nullptr;
            _tickInterval     = 1.0 / 60.0;
        }

        // Singleton setters
        static void SetNetworkPeer(Networking::NetworkPeer *peer) {
            FW_ASSERT_MODULE_REGISTRATION(_networkPeer, peer, "NetworkPeer");
            _networkPeer = peer;
        }

        static void SetReplication(Networking::Replication::ReplicationManager *replication) {
            FW_ASSERT_MODULE_REGISTRATION(_replication, replication, "Replication");
            _replication = replication;
        }

        static void SetVoiceServer(Voice::VoiceServer *voice) {
            FW_ASSERT_MODULE_REGISTRATION(_voiceServer, voice, "VoiceServer");
            _voiceServer = voice;
        }

        static void SetScriptingModule(Scripting::ScriptingModule *module) {
            FW_ASSERT_MODULE_REGISTRATION(_scriptingModule, module, "ScriptingModule");
            _scriptingModule = module;
        }

        static void SetWebManager(GUI::Manager *manager) {
            FW_ASSERT_MODULE_REGISTRATION(_webManager, manager, "WebManager");
            _webManager = manager;
        }

        static void SetInput(Input::IInput *input) {
            FW_ASSERT_MODULE_REGISTRATION(_input, input, "Input");
            _input = input;
        }

        static void SetClientInstance(Integrations::Client::Instance *instance) {
            FW_ASSERT_MODULE_REGISTRATION(_clientInstance, instance, "ClientInstance");
            _clientInstance = instance;
        }

        static void SetTickInterval(double seconds) noexcept {
            _tickInterval = (seconds > 0.0) ? seconds : (1.0 / 60.0);
        }

        // Singleton getters
        static Networking::NetworkPeer *GetNetworkPeer() noexcept {
            return _networkPeer;
        }

        static Networking::Replication::ReplicationManager *GetReplication() noexcept {
            return _replication;
        }

        static Voice::VoiceServer *GetVoiceServer() noexcept {
            return _voiceServer;
        }

        static Scripting::ScriptingModule *GetScriptingModule() noexcept {
            return _scriptingModule;
        }

        static GUI::Manager *GetWebManager() noexcept {
            return _webManager;
        }

        static Input::IInput *GetInput() noexcept {
            return _input;
        }

        static Integrations::Client::Instance *GetClientInstance() noexcept {
            return _clientInstance;
        }

        static double GetTickInterval() noexcept {
            return _tickInterval;
        }

      private:
        static inline Networking::NetworkPeer *_networkPeer {};
        static inline Networking::Replication::ReplicationManager *_replication {};
        static inline Voice::VoiceServer *_voiceServer {};
        static inline Scripting::ScriptingModule *_scriptingModule {};
        static inline GUI::Manager *_webManager {};
        static inline Input::IInput *_input {};
        static inline Integrations::Client::Instance *_clientInstance {};
        static inline double _tickInterval {1.0 / 60.0};
    };
} // namespace Framework
