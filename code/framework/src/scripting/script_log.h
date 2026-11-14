/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "script_engine.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Framework::Scripting {
    // How many entries of one resource LogScriptError writes in a second, and how long the text of one may be:
    // what a Lua runtime allows its output and its errors (LuaRuntime::kMaxLinesPerSecond, kMaxErrorBytes).
    inline constexpr int kMaxScriptErrorsPerSecond = 200;
    inline constexpr size_t kMaxScriptErrorBytes   = 16384;

    // A space for each control character but the tab, so that a script cannot start a line of its own in the
    // log or drive the terminal that shows it. That covers the C1 controls as UTF-8 spells them.
    void BlankControlCharacters(std::string &line);

    // A name a script chose (an event, a key, an export), for one line of the log: its control characters
    // blanked, line ends included.
    std::string BlankedLine(std::string_view text);

    // A text a script chose, an error and its stack as a rule, for one entry of the log: kMaxScriptErrorBytes of
    // it at most, control characters blanked, and each line after the first started with `continuation`, so that
    // none of them passes for an entry of its own.
    std::string SanitisedScriptText(std::string_view text, std::string_view continuation);

    // Writes what a script of a resource failed with to the scripting logger, as one entry:
    // "[resource] <text>", sanitised as above with every further line under the name of the resource too.
    // A resource that fails in a loop gets kMaxScriptErrorsPerSecond entries in a second, then one warning that
    // the rest of that second is left out; the first entry after it says how many were. `owner` tells the
    // resources of one manager from those of another that have the same name: the manager, or null where there
    // is none. It is a key and is never read through. With no resource name the entry has no "[resource] " and
    // its further lines are indented. Call it on any thread.
    void LogScriptError(const void *owner, std::string_view resourceName, std::string_view text, OutputLevel level = OutputLevel::Error);

    // Gives a resource that starts its whole share again: what the run before it used up is not held against it.
    void ResetScriptErrorBudget(const void *owner, std::string_view resourceName);

    // Forgets every share kept for `owner`, whose address the next one may get.
    void ForgetScriptErrorBudgets(const void *owner);
} // namespace Framework::Scripting
