/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "execution_environment.h"
#include "../scripting_catalog.h"
#include <utils/version.h>

#include <v8pp/convert.hpp>

namespace Framework::Scripting::Builtins {

    void ExecutionEnvironment::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, bool isClient, const std::string &modVersion) {
        v8pp::module env(isolate);
        env.const_("isClient", isClient);
        env.const_("isServer", !isClient);
        env.const_("frameworkVersion", std::string(Framework::Utils::Version::rel));
        env.const_("modVersion", modVersion);

        target->Set(context, v8pp::to_v8(isolate, "ExecutionEnvironment"), env.new_instance()).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("ExecutionEnvironment", "Runtime-side flags and local release versions exposed as the global ExecutionEnvironment.");
        metadata.add_property("isClient", "boolean", "True in the sandboxed client scripting runtime.", true);
        metadata.add_property("isServer", "boolean", "True in the authoritative server scripting runtime.", true);
        metadata.add_property("frameworkVersion", "string", "Release version of the Framework running this script. This is the local client or server version, not the remote peer's version.", true);
        metadata.add_property("modVersion", "string", "Multiplayer mod version supplied by the local application through InstanceOptions.modVersion. Empty when no mod version was supplied. This is not the underlying game's version or the remote peer's version.", true);
    }

} // namespace Framework::Scripting::Builtins
