/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "environment.h"
#include "../scripting_catalog.h"

#include <v8pp/convert.hpp>

namespace Framework::Scripting::Builtins {

    void Environment::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, bool isClient) {
        v8pp::module env(isolate);
        env.const_("IsClient", isClient);
        env.const_("IsServer", !isClient);

        target->Set(context, v8pp::to_v8(isolate, "Environment"), env.new_instance()).Check();

        auto &metadata = GetScriptingCatalog(isolate).global_object("Environment", "Runtime-side flags exposed as Core.Environment.");
        metadata.add_property("IsClient", "boolean", "True in the sandboxed client scripting runtime.", true);
        metadata.add_property("IsServer", "boolean", "True in the authoritative server scripting runtime.", true);
    }

} // namespace Framework::Scripting::Builtins
