#pragma once

#include <set>
#include <string>
#include <string_view>

namespace Framework::Scripting {

    /**
     * Reserved event names that cannot be emitted by user code.
     * Only the framework can emit these via EmitReserved().
     */
    inline const std::set<std::string, std::less<>> RESERVED_EVENTS = {
        "resourceStart",
        "resourceStop",
        "resourceError",
        "playerConnect",
        "playerDisconnect",
        "playerSpawn",
        "serverStart",
        "serverStop"
    };

    inline bool IsReservedEvent(std::string_view eventName) {
        return RESERVED_EVENTS.contains(eventName);
    }

} // namespace Framework::Scripting
