#pragma once

#include <map>
#include <string>

namespace Framework::Scripting {
    enum class EventIDs { RESOURCE_LOADED, RESOURCE_RELOADED, RESOURCE_UNLOADING };

    std::map<EventIDs, std::string> Events = {{EventIDs::RESOURCE_LOADED, "resourceLoaded"},
                                              {EventIDs::RESOURCE_RELOADED, "resourceReloaded"},
                                              {EventIDs::RESOURCE_UNLOADING, "resourceUnloading"}};
} // namespace Framework::Scripting
