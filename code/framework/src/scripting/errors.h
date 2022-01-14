/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Scripting {
    enum class EngineError {
        ENGINE_NONE,
        ENGINE_NODE_INIT_FAILED,
        ENGINE_PLATFORM_INIT_FAILED,
        ENGINE_V8_INIT_FAILED,
        ENGINE_UV_LOOP_INIT_FAILED,
        ENGINE_ISOLATE_ALLOCATION_FAILED,
        ENGINE_ISOLATE_DATA_ALLOCATION_FAILED,
        ENGINE_PLATFORM_NULL,
        ENGINE_ISOLATE_NULL
    };

    enum class ResourceManagerError { RESOURCE_MANAGER_NONE, RESOURCE_ALREADY_LOADED, RESOURCE_NOT_LOADED, RESOURCE_LOADING_FAILED, RESOURCE_NAME_INVALID };

    enum class BuiltinError { BUILTIN_NONE, BUILTIN_INVALID_PARAMETERS_COUNT, BUILTIN_INVALID_PARAMETER, BUILTIN_ISOLATE_NULL, BUILTIN_CONTEXT_EMPTY, BUILTIN_RESOURCE_NULL };

    enum class V8HelperError { HELPER_NONE, HELPER_ISOLATE_NULL, HELPER_CONTEXT_EMPTY, HELPER_REGISTER_FAILED, HELPER_TEMPLATE_ALREADY_LOADED, HELPER_ALREADY_LOADED };
} // namespace Framework::Scripting
