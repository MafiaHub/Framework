/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "execution_environment.h"
#include "../lua/lua_runtime.h"
#include "../scripting_catalog.h"
#include <utils/version.h>

namespace Framework::Scripting::Builtins {

    namespace {
        Bind::Namespace::RuntimeConstants ConstantsOf(bool isClient, const std::string &modVersion) {
            return {{"isClient", isClient}, {"isServer", !isClient}, {"modVersion", modVersion}};
        }
    } // namespace

    const Bind::Namespace &ExecutionEnvironment::Definition() {
        static const auto definition = Bind::Namespace("ExecutionEnvironment", "Runtime-side flags exposed as the global ExecutionEnvironment.")
                                           .RuntimeConstant<bool>("isClient", "True in the sandboxed client scripting runtime.")
                                           .RuntimeConstant<bool>("isServer", "True in the authoritative server scripting runtime.")
                                           .Constant("frameworkVersion", Framework::Utils::Version::rel, "Local Framework release version.")
                                           .RuntimeConstant<std::string>("modVersion", "Local mod version from InstanceOptions.modVersion; empty when unset.");
        return definition;
    }

    void ExecutionEnvironment::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, bool isClient, const std::string &modVersion) {
        Definition().MaterialiseV8(isolate, context, target, GetScriptingCatalog(isolate), isClient ? Bind::Side::Client : Bind::Side::Server, ConstantsOf(isClient, modVersion));
    }

    void ExecutionEnvironment::RegisterLua(LuaRuntime &runtime, const std::string &modVersion) {
        const Bind::Side side = runtime.Options().side;
        Definition().RegisterLuaGlobal(runtime.State(), side, ConstantsOf(side == Bind::Side::Client, modVersion));
    }

} // namespace Framework::Scripting::Builtins
