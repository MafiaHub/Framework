/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "events.h"

#include <map>
#include <string>

namespace Framework::Scripting {
    std::map<EventIDs, std::string> Events = {{EventIDs::RESOURCE_LOADED, "resourceLoaded"}, {EventIDs::RESOURCE_UNLOADING, "resourceUnloading"}, {EventIDs::RESOURCE_UPDATED, "resourceUpdated"}};
} // namespace Framework::Scripting
