/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "events.h"

#include <map>
#include <string>

namespace Framework::Scripting {
    std::map<EventIDs, std::string> Events = {{EventIDs::GAMEMODE_LOADED, "onGamemodeLoaded"}, {EventIDs::GAMEMODE_UNLOADING, "onGamemodeUnloading"}, {EventIDs::GAMEMODE_UPDATED, "onGamemodeUpdated"}};
} // namespace Framework::Scripting
