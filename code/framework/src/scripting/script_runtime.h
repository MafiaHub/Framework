/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "value_transfer.h"

#include <utils/result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {
    class Resource;

    // Which handler table an event goes to. Client events (raised from a client connection) reach only handlers that
    // asked for client events, never the server bus, in every language.
    enum class EventScope : uint8_t {
        Global,
        Client,
    };

    /**
     * A language other than JavaScript that server resources can be written in (docs/scripting_csharp.md).
     *
     * A resource's manifest names its runtime ("serverRuntime"); the resource manager starts and stops it through
     * this interface and keeps everything else (dependencies, state, restarts, lifecycle events) as it does for
     * JavaScript. Values reach the runtime as TransferredValue, the same copies that cross between JavaScript
     * resources, so a JavaScript and a C# resource share events and call each other's exports.
     *
     * Every call comes from the scripting thread.
     */
    class ScriptRuntime {
      public:
        virtual ~ScriptRuntime() = default;

        // The manifest's "serverRuntime" value that selects this runtime.
        virtual const std::string &GetName() const = 0;

        /**
         * Load a resource from its server scripts (absolute paths, already checked to lie inside the resource) and run
         * its start. On failure nothing of the resource stays loaded.
         */
        virtual Utils::Result<void, std::string> StartResource(const Resource &resource, const std::vector<std::string> &scripts) = 0;

        // Run the resource's stop and unload it, with everything it registered. A no-op for a resource not loaded.
        virtual void StopResource(const std::string &resourceName) = 0;

        virtual bool IsResourceLoaded(const std::string &resourceName) const = 0;

        // Continuations, timers and other deferred work, once per server tick.
        virtual void Tick() = 0;

        // Stop every resource and release the runtime.
        virtual void Shutdown() = 0;

        // Whether a handler in this runtime listens for the event, so the bus copies arguments only when one does.
        virtual bool WantsEvent(EventScope scope, std::string_view eventName) const = 0;

        /**
         * Deliver an event to this runtime's handlers, only those of `targetResource` when it is not empty.
         * @return false when a handler vetoed it (returned false); the bus honours that only for synchronous emits.
         */
        virtual bool DispatchEvent(EventScope scope, const std::string &eventName, const std::string &targetResource, const std::vector<TransferredValue> &args) = 0;

        virtual bool HasExport(const std::string &resourceName, const std::string &exportName) const = 0;

        // Call a resource's export with copied arguments; a value export ignores them and returns itself.
        virtual Utils::Result<TransferredValue, std::string> CallExport(const std::string &resourceName, const std::string &exportName, const std::vector<TransferredValue> &args) = 0;
    };
} // namespace Framework::Scripting
