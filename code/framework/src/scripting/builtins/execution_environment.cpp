/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "execution_environment.h"
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

    void ExecutionEnvironment::Register(Bind::Registrar &registrar, Bind::Languages languages, const std::string &modVersion) {
        registrar.Global(Definition(), languages, [modVersion](Bind::Side side) {
            return ConstantsOf(side == Bind::Side::Client, modVersion);
        });
    }

} // namespace Framework::Scripting::Builtins
