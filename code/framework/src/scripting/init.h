/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <functional>

namespace Framework::Scripting {
    class SDK;
    using SDKRegisterCallback = std::function<void(SDK *)>;
} // namespace Framework::Scripting
