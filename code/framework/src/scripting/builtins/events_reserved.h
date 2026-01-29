#pragma once

#include <set>
#include <string>

namespace Framework::Scripting {

    /**
     * Reserved event names that cannot be emitted by user code.
     * Only the framework can emit these via EmitReserved().
     */
    inline const std::set<std::string> RESERVED_EVENTS = {
        "resourceStart",
        "resourceStop",
        "resourceError",
        "playerConnect",
        "playerDisconnect",
        "playerSpawn",
        "serverStart",
        "serverStop"
    };

    inline bool IsReservedEvent(const std::string &eventName) {
        return RESERVED_EVENTS.find(eventName) != RESERVED_EVENTS.end();
    }

} // namespace Framework::Scripting
