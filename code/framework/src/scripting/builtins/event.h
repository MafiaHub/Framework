#pragma once

#include "core_modules.h"

#include "../resource/environment_sandbox.h"
#include "../resource/resource_manager.h"

namespace Framework::Scripting::Builtins {

    /**
     * Event builtin for cross-resource events.
     */
    class Event final {
        /**
         * Register a global event handler.
         * This handler will be invoked when any resource broadcasts this event.
         */
        static void OnGlobal(const std::string &name, sol::protected_function handler) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->RegisterGlobalEventHandler(name, handler);
            }
        }

        /**
         * Broadcast a global event to all running resources.
         */
        static void Broadcast(const std::string &name, sol::variadic_args args) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->BroadcastGlobalEvent(name, args);
            }
        }

        /**
         * Register a targeted event handler.
         * This handler will be invoked when another resource emits an event to this resource.
         */
        static void OnTargeted(const std::string &name, sol::protected_function handler) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->RegisterTargetedEventHandler(name, handler);
            }
        }

        /**
         * Emit an event to a specific resource.
         */
        static void EmitTo(const std::string &targetResource, const std::string &name, sol::variadic_args args) {
            auto *manager = Framework::CoreModules::GetResourceManager();
            if (manager) {
                manager->EmitTargetedEvent(targetResource, name, args);
            }
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Event> cls = luaEngine->new_usertype<Event>("Event");

            cls["onGlobal"]  = &Event::OnGlobal;
            cls["broadcast"] = &Event::Broadcast;

            cls["onTargeted"] = &Event::OnTargeted;
            cls["emitTo"]     = &Event::EmitTo;
            EnvironmentSandbox::RegisterBuiltinName("Event");
        }
    };

} // namespace Framework::Scripting::Builtins
