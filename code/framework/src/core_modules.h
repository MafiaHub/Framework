/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Networking {
    class NetworkPeer;
}; // namespace Framework::Networking

namespace Framework::World {
    class Engine;
}; // namespace Framework::World

namespace Framework::Scripting {
    class Module;
}; // namespace Framework::Scripting

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
            _scriptingModule = nullptr;
        }

        // Singleton setters
        static void SetNetworkPeer(Framework::Networking::NetworkPeer *peer) {
            _networkPeer = peer;
        }

        static void SetWorldEngine(Framework::World::Engine *engine) {
            _engine = engine;
        }

        static void SetScriptingModule(Framework::Scripting::Module *module) {
            _scriptingModule = module;
        }

        // Singleton getters
        static Framework::Networking::NetworkPeer *GetNetworkPeer() {
            return _networkPeer;
        }

        static Framework::World::Engine *GetWorldEngine() {
            return _engine;
        }

        static Framework::Scripting::Module *GetScriptingModule() {
            return _scriptingModule;
        }

      private:
        static inline Framework::Networking::NetworkPeer *_networkPeer {};
        static inline Framework::World::Engine *_engine {};
        static inline Framework::Scripting::Module *_scriptingModule {};
    };
}; // namespace Framework
