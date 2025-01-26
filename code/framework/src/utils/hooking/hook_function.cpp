/*
 * MafiaHub OSS license
 * Copyright (c) 2020, CitizenFX
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "hook_function.h"

static HookFunctionBase *g_hookFunctions;

void HookFunctionBase::Register() {
    _next          = g_hookFunctions;
    g_hookFunctions = this;
}

void HookFunctionBase::RunAll() {
    for (auto func = g_hookFunctions; func; func = func->_next) {
        func->Run();
    }
}

static RuntimeHookFunction *g_runtimeHookFunctions;

void RuntimeHookFunction::Register() {
    _next                 = g_runtimeHookFunctions;
    g_runtimeHookFunctions = this;
}

void RuntimeHookFunction::Run(const char *key) {
    for (auto func = g_runtimeHookFunctions; func; func = func->_next) {
        if (func->_key == key) {
            func->_function();
        }
    }
}
