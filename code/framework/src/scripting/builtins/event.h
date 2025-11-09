#pragma once

#include "core_modules.h"

#include "../engine.h"

namespace Framework::Scripting::Builtins {
    class Event final {
        static void On(const std::string name, const sol::function fnc) {
            Framework::CoreModules::GetScriptingEngine()->ListenEvent(name, fnc);
        }

        static void Remove(const std::string &name, const sol::function &fnc) {
            Framework::CoreModules::GetScriptingEngine()->RemoveEventListener(name, fnc);
        }

        static void Emit(const std::string name, sol::variadic_args args) {
            Framework::CoreModules::GetScriptingEngine()->InvokeEvent(name, args);
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Event> cls = luaEngine->new_usertype<Event>("Event");
            cls["on"]                = &Event::On;
            cls["remove"]            = &Event::Remove;
            cls["emit"]              = &Event::Emit;
        }
    };
} // namespace Framework::Scripting::Builtins
