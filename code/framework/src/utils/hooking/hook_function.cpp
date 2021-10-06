/*
 * MafiaHub OSS license
 * Copyright (c) 2021 MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "hook_function.h"

static HookFunctionBase* g_hookFunctions;

void HookFunctionBase::Register()
{
    m_next = g_hookFunctions;
    g_hookFunctions = this;
}

void HookFunctionBase::RunAll()
{
    for (auto func = g_hookFunctions; func; func = func->m_next)
    {
        func->Run();
    }
}

static RuntimeHookFunction* g_runtimeHookFunctions;

void RuntimeHookFunction::Register()
{
    m_next = g_runtimeHookFunctions;
    g_runtimeHookFunctions = this;
}

void RuntimeHookFunction::Run(const char* key)
{
    for (auto func = g_runtimeHookFunctions; func; func = func->m_next)
    {
        if (func->m_key == key)
        {
            func->m_function();
        }
    }
}