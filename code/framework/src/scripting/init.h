/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2.hpp>

namespace Framework::Scripting {
    class SDK;
    using SDKRegisterCallback = fu2::function<void(SDK *) const>;
} // namespace Framework::Scripting
