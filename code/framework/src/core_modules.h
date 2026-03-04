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

namespace Framework::World {
    class Engine;
} // namespace Framework::World


namespace Framework::GUI {
    class Manager;
} // namespace Framework::GUI

namespace Framework::Input {
    class IInput;
} // namespace Framework::Input

namespace Framework {

#define FW_ASSERT_MODULE_REGISTRATION(current, incoming, name)                \
    do {                                                                       \
        if ((current) != nullptr && (incoming) != nullptr) {                   \
            assert(false && name " is already registered in CoreModules");     \
        }                                                                      \
    } while (0)

    /**
     * @brief Class that couples modules together
     *
     * It registers and provides an easy way to access modules from each other.
     */
    class CoreModules final {
      public:
        static void Reset() {
            _networkPeer      = nullptr;
            _engine           = nullptr;
            _scriptingModule  = nullptr;
            _webManager       = nullptr;
            _input            = nullptr;
        }

        // Singleton setters
        static void SetNetworkPeer(Networking::NetworkPeer *peer) {
            FW_ASSERT_MODULE_REGISTRATION(_networkPeer, peer, "NetworkPeer");
            _networkPeer = peer;
        }

        static void SetWorldEngine(World::Engine *engine) {
            FW_ASSERT_MODULE_REGISTRATION(_engine, engine, "WorldEngine");
            _engine = engine;
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

        static Scripting::ScriptingModule *GetScriptingModule() {
            return _scriptingModule;
        }

        static GUI::Manager *GetGUIManager() {
            return _webManager;
        }

        static Input::IInput *GetInput() {
            return _input;
        }

        static double GetTickRate() {
            return _tickRate;
        }

      private:
        static inline Networking::NetworkPeer *_networkPeer {};
        static inline World::Engine *_engine {};
        static inline Scripting::ScriptingModule *_scriptingModule {};
        static inline GUI::Manager *_webManager {};
        static inline Input::IInput *_input {};
        static inline double _tickRate {1000 / 60.0f};
    };
} // namespace Framework
