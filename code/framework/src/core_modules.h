/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Networking {
    class NetworkPeer;
} // namespace Framework::Networking

namespace Framework::World {
    class Engine;
} // namespace Framework::World

namespace Framework::Scripting {
    class Engine;
} // namespace Framework::Scripting

namespace Framework::GUI {
    class Manager;
} // namespace Framework::GUI

namespace Framework {

    /**
     * @brief Class that couples modules together
     *
     * It registers and provides an easy way to access modules from each other.
     */
    class CoreModules final {
      public:
        static void Reset() {
            _networkPeer     = nullptr;
            _engine          = nullptr;
            _scriptingEngine = nullptr;
        }

        // Singleton setters
        static void SetNetworkPeer(Networking::NetworkPeer *peer) {
            _networkPeer = peer;
        }

        static void SetWorldEngine(World::Engine *engine) {
            _engine = engine;
        }

        static void SetScriptingEngine(Scripting::Engine *engine) {
            _scriptingEngine = engine;
        }

        static void SetWebManager(GUI::Manager *manager) {
            _webManager = manager;
        }

        static void SetTickRate(double tickRate) {
            _tickRate = tickRate;
        }

        // Singleton getters
        static Networking::NetworkPeer *GetNetworkPeer() {
            return _networkPeer;
        }

        static World::Engine *GetWorldEngine() {
            return _engine;
        }

        static Scripting::Engine *GetScriptingEngine() {
            return _scriptingEngine;
        }

        static GUI::Manager *GetGUIManager() {
            return _webManager;
        }

        static double GetTickRate() {
            return _tickRate;
        }

      private:
        static inline Networking::NetworkPeer *_networkPeer {};
        static inline World::Engine *_engine {};
        static inline Scripting::Engine *_scriptingEngine {};
        static inline GUI::Manager *_webManager {};
        static inline double _tickRate {1000 / 60.0f};
    };
} // namespace Framework
