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
    std::map<EventIDs, std::string> Events = {{EventIDs::GAMEMODE_LOADED, "gamemodeLoaded"}, {EventIDs::GAMEMODE_UNLOADING, "gamemodeUnloading"}, {EventIDs::GAMEMODE_UPDATED, "gamemodeUpdated"}};
} // namespace Framework::Scripting
