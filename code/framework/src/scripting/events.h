/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <string>

namespace Framework::Scripting {
    enum class EventIDs { RESOURCE_LOADED, RESOURCE_UNLOADING };

    extern std::map<EventIDs, std::string> Events;
} // namespace Framework::Scripting
