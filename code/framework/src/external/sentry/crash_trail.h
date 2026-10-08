/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::External::Sentry {
    /**
     * Writes where a crash happened into the log before crashpad takes it.
     *
     * Crashpad installs itself as the process's top-level exception filter
     * and reports out of process, so a crash on any thread but the one the
     * launcher wraps in `__try` -- a game job worker, a render thread --
     * reaches it without a line in the log. Sentry has no unwind tables for
     * the game's modules either, so its own stack for such a crash is a row
     * of unknown frames. This sits in front of crashpad and writes the
     * faulting module and offset, the registers, the thread's name and a
     * stack walked with each module's own unwind data, then hands the crash
     * on unchanged. Crashpad reads the log attachment when it processes the
     * dump, so the trail rides along with the report.
     *
     * The same trail also goes onto the Sentry scope as the `crash_trail`
     * context, a field per frame. The log line it writes becomes a breadcrumb
     * as well, but a breadcrumb message is cut far short of a whole trail --
     * the headline survives and the stack does not -- so the context is what
     * carries the callers into a report whose attachment never arrives.
     *
     * Windows only; elsewhere this does nothing.
     */
    class CrashTrail final {
      public:
        /** Chains in front of the filter installed now. Call once crashpad is up. */
        static void Install();
    };
} // namespace Framework::External::Sentry
