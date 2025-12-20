/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "events.h"

namespace Framework::Scripting {
    std::map<EventIDs, std::string> Events = {{EventIDs::RESOURCE_STARTED, "onResourceStarted"}, {EventIDs::RESOURCE_STOPPING, "onResourceStopping"}, {EventIDs::RESOURCE_UPDATED, "onResourceUpdated"}};
} // namespace Framework::Scripting
