#pragma once

#include <map>
#include <string>

namespace Framework::Scripting {
    enum class EventIDs { RESOURCE_LOADED, RESOURCE_UPDATE, RESOURCE_UNLOADING };

    std::map<EventIDs, std::string> Events = {{EventIDs::RESOURCE_LOADED, "resourceLoaded"},
                                              {EventIDs::RESOURCE_UPDATE, "resourceUpdate"},
                                              {EventIDs::RESOURCE_UNLOADING, "resourceUnloading"}};
} // namespace Framework::Scripting
