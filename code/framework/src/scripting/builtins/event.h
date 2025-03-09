#pragma once

#include "core_modules.h"

#include "../module.h"
#include "../types/events.h"

namespace Framework::Scripting::Builtins {
    class Event final {
        static void On(const std::string name, const sol::function fnc) {
            Framework::CoreModules::GetScriptingModule()->GetEngine()->ListenEvent(name, fnc);
        }

        static void Emit(const std::string name, sol::variadic_args args) {
            Framework::CoreModules::GetScriptingModule()->GetEngine()->InvokeEvent(name, args);
        }

      public:
        static void Register(sol::state &luaEngine) {
            sol::usertype<Event> cls = luaEngine.new_usertype<Event>("Event");
            cls["on"]                = &Event::On;
            cls["emit"]              = &Event::Emit;
        }
    };
} // namespace Framework::Scripting::Builtins
