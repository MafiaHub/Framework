/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8pp/metadata.hpp>

#include <v8.h>

namespace Framework::Scripting {
    // The timer globals, declared in the framework's own catalog.
    //
    // Neither side installs them through a binding that could record itself: the client's V8Engine
    // sets them straight onto the global, and the server's are Node's own, wrapped in JavaScript for
    // per-resource cleanup. Left undeclared, every resource had to restate them, because a resource
    // compiles against ES2022 alone and neither the DOM's nor Node's declarations apply to it.
    //
    // The two sides really do differ in the handle: the client hands back a numeric id, the server
    // Node's Timeout object, which carries ref and unref.
    inline void RegisterTimerMetadata(v8pp::metadata::registry &catalog, bool isClient) {
        const char *handle = isClient ? "number" : "Timeout";
        if (!isClient) {
            auto &timeout = catalog.data_type("Timeout", "Node.js timer handle returned by setTimeout and setInterval.");
            timeout.record(v8pp::metadata::function_of<v8::FunctionCallback>("ref", v8pp::metadata::docs("Timeout", {}, "Keeps the event loop alive while this timer is pending.", "This timer.")));
            timeout.record(v8pp::metadata::function_of<v8::FunctionCallback>("unref", v8pp::metadata::docs("Timeout", {}, "Lets the event loop exit while this timer is still pending.", "This timer.")));
            timeout.record(v8pp::metadata::function_of<v8::FunctionCallback>("hasRef", v8pp::metadata::docs("boolean", {}, "Whether this timer keeps the event loop alive.")));
            timeout.record(v8pp::metadata::function_of<v8::FunctionCallback>("refresh", v8pp::metadata::docs("Timeout", {}, "Restarts this timer's countdown from now, with its original delay.", "This timer.")));
        }

        const auto schedule = [&catalog, handle](const char *name, const char *description) {
            catalog.function_(v8pp::metadata::function_of<v8::FunctionCallback>(name,
                v8pp::metadata::docs(handle,
                    {
                        v8pp::metadata::param("handler", "(...args: any[]) => void", false, "Function to call."),
                        v8pp::metadata::param("milliseconds", "number", true, "Delay before the call."),
                        v8pp::metadata::rest_param("args", "unknown[]", "Arguments passed to the handler."),
                    },
                    description, "Handle to pass to the matching clear function.")));
        };
        schedule("setTimeout", "Calls a function once after a delay.");
        schedule("setInterval", "Calls a function repeatedly, waiting the delay between calls.");

        const auto cancel = [&catalog, handle](const char *name, const char *description) {
            catalog.function_(v8pp::metadata::function_of<v8::FunctionCallback>(name, v8pp::metadata::docs("void", {v8pp::metadata::param("handle", handle, true, "Handle returned when the timer was scheduled; anything else is ignored.")}, description)));
        };
        cancel("clearTimeout", "Cancels a pending setTimeout.");
        cancel("clearInterval", "Cancels a setInterval.");

        catalog.function_(v8pp::metadata::function_of<v8::FunctionCallback>("queueMicrotask", v8pp::metadata::docs("void", {v8pp::metadata::param("callback", "() => void", false, "Function to run once the current task completes.")}, "Queues a function on the microtask queue.")));
    }
} // namespace Framework::Scripting
