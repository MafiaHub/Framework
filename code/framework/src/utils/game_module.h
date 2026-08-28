/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "safe_win32.h"

#include <function2.hpp>
#include <string>

namespace Framework::Utils {
    /**
     * Resolves the module carrying the game's code and rebases the pattern scanner
     * onto it.
     *
     * Games that keep their code in a module other than the executable (CryEngine
     * titles ship a bootstrap that loads it later) cannot resolve anything until
     * that module is mapped. See docs/cryengine_games.md.
     */
    class GameModule final {
      public:
        using ReadyProc = fu2::unique_function<void(HMODULE gameModule)>;

        /**
         * Invokes onReady once, with hook::set_base already pointed at the module.
         *
         * An empty moduleName selects the executable and runs onReady inline;
         * otherwise onReady runs on a worker thread once the loader reports the
         * module, or not at all if it never loads.
         */
        static void WhenReady(const std::wstring &moduleName, ReadyProc onReady);

        /** How long WhenReady waits for a named module before giving up. */
        static constexpr DWORD kWaitTimeoutMs = 120000;
    };
} // namespace Framework::Utils
