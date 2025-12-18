#pragma once

#include "core_modules.h"

#include "../engine.h"
#include "../resource/environment_sandbox.h"

namespace Framework::Scripting::Builtins {
    class Environment final {
        static std::string Platform() {
#ifdef _WIN32
            return "Windows";
#elif __APPLE__
            return "macOS";
#else
            return "Linux";
#endif
        }

        static std::string Build() {
#ifndef _DEBUG
            return "Release";
#else
            return "Debug";
#endif
        }

        static std::string Release() {
            // todo: use MH_PROD for stable releases on GitHub
#ifdef MH_PROD
            return "Production";
#else
            return "Development";
#endif
        }

      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Environment> cls = luaEngine->new_usertype<Environment>("Environment");
            cls["platform"]                = &Environment::Platform;
            cls["build"]                   = &Environment::Build;
            cls["release"]                 = &Environment::Release;
            EnvironmentSandbox::RegisterBuiltinName("Environment");
        }
    };
} // namespace Framework::Scripting::Builtins
