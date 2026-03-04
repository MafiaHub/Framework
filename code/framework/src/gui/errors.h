/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::GUI {
    enum class GUIError {
        GUI_NONE,
        GUI_CEF_INIT_FAILED,
        GUI_RENDERER_NULL,
        GUI_VIEW_INIT_FAILED
    };
} // namespace Framework::GUI
