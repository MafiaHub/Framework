/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Graphics {
    enum class RendererBackend { BACKEND_D3D_9, BACKEND_D3D_11, BACKEND_D3D_12 };

    enum class RendererAPI { CEF, IMGUI, ULTRALIGHT };

    enum class RendererState { STATE_NOT_INITIALIZED, STATE_READY, STATE_DEVICE_LOST, STATE_DEVICE_NOT_RESET, STATE_DRIVER_ERROR };
} // namespace Framework::Graphics
