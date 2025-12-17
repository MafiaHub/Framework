#pragma once

#include "core_modules.h"

#include "../engine.h"
#include "../resource/resource_manager.h"

namespace Framework::Scripting::Builtins {

    /**
     * Event builtin for local and cross-resource events.
     *
     * Lua API:
     *   -- Local events (current behavior, isolated to resource or legacy mode)
     *   Event.on(name, handler)               -- Register local event handler
     *   Event.emit(name, ...)                 -- Emit local event
     *
     *   -- Global events (all resources can subscribe)
     *   Event.onGlobal(name, handler)         -- Register global event handler
     *   Event.broadcast(name, ...)            -- Broadcast global event to all resources
     *
     *   -- Targeted events (sent to specific resource)
     *   Event.onTargeted(name, handler)       -- Register targeted event handler
     *   Event.emitTo(resource, name, ...)     -- Emit event to specific resource
     */
    class Event final {
        /**
         * Register a local event handler (legacy behavior).
         */
        static void On(const std::string &name, const sol::function fnc) {
            Framework::CoreModules::GetScriptingEngine()->ListenEvent(name, fnc);
        }

        /**
         * Emit a local event (legacy behavior).
         */
        static void Emit(const std::string &name, sol::variadic_args args) {
            Framework::CoreModules::GetScriptingEngine()->InvokeEvent(name, args);
        }

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

            // Local events (legacy behavior)
            cls["on"]   = &Event::On;
            cls["emit"] = &Event::Emit;

            // Global events (Phase 3)
            cls["onGlobal"]  = &Event::OnGlobal;
            cls["broadcast"] = &Event::Broadcast;

            // Targeted events (Phase 3)
            cls["onTargeted"] = &Event::OnTargeted;
            cls["emitTo"]     = &Event::EmitTo;
        }
    };

} // namespace Framework::Scripting::Builtins
