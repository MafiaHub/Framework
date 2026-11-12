/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <string_view>

namespace Framework::Scripting {
    // How many errors of one resource LogScriptError writes in a second: as many as a Lua runtime writes lines
    // of output (LuaRuntime::kMaxLinesPerSecond).
    inline constexpr int kMaxScriptErrorsPerSecond = 200;

    // A space for each control character but the tab, so that a script cannot start a line of its own in the
    // log or drive the terminal that shows it. That covers the C1 controls as UTF-8 spells them.
    void BlankControlCharacters(std::string &line);

    // Writes what a script of a resource failed with to the scripting logger, as one error entry:
    // "[resource] <text>". A script chooses the text of its errors as it chooses what it prints, so each further
    // line of the text is put under the name of the resource too, and control characters are blanked. A resource
    // that fails in a loop gets kMaxScriptErrorsPerSecond entries in a second, then one warning that the rest of
    // that second is left out. Call it on the scripting thread or any other.
    void LogScriptError(std::string_view resourceName, std::string_view text);
} // namespace Framework::Scripting
