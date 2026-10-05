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
        env.const_("frameworkVersion", Framework::Utils::Version::rel);
        env.const_("modVersion", modVersion);

        target->Set(context, v8pp::to_v8(isolate, "ExecutionEnvironment"), env.new_instance()).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("ExecutionEnvironment", "Runtime-side flags exposed as the global ExecutionEnvironment.");
        metadata.add_property("isClient", "boolean", "True in the sandboxed client scripting runtime.", true);
        metadata.add_property("isServer", "boolean", "True in the authoritative server scripting runtime.", true);
        metadata.add_property("frameworkVersion", "string", "Local Framework release version.", true);
        metadata.add_property("modVersion", "string", "Local mod version from InstanceOptions.modVersion; empty when unset.", true);
    }

} // namespace Framework::Scripting::Builtins
