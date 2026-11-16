/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2/function2.hpp>

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace Framework::Utils {
    // Who may touch the world, between a tick thread and a script thread.
    //
    // A server that runs its scripts on a thread of their own still has one world, so one side has it at a time.
    // What makes that worth doing is how it changes hands: the tick never waits for a script to finish. It asks,
    // and takes the world at the script's next safe point, which is between two tasks or in the middle of one (a
    // script stopped between two of its own instructions keeps its place and carries on when the tick is done). A
    // script that computes for a second costs the tick the distance to its next safe point, not the second.
    //
    // The script side is one thread. The tick side is whichever other thread calls it, one at a time.
    class WorldLock final {
      public:
        // Called on the tick side, with nothing held, when a tick has to wait for the script side, and again every
        // millisecond or so for as long as it waits: the hook is how a script that is mid-task gets told to reach
        // a safe point, and a script that could not take one when first asked may be able to by the next time.
        void SetContendedHook(fu2::function<void()> hook);

        // --- Tick side ---
        void LockForTick();
        void UnlockFromTick();

        // While holding the world: make sure the script side is in no task at all, so nothing of a script is on its
        // stack, and keep it out until ReadmitScript(). For a tick that is about to run script itself. A script that
        // had stopped mid-task for this tick is lent the world to finish that task first, so this can take as long
        // as the task does. Lent to the script only: no other thread on the tick side gets the world meanwhile.
        // Calls nest.
        void EvictScript();
        void ReadmitScript();

        // --- Script side ---
        // False once Close() was called: the script side is being shut down.
        bool LockForScript();
        void UnlockFromScript();

        // Mid-task, at a point where the world may change under the script: if a tick is waiting, lets it run and
        // takes the world back. True if it did.
        bool YieldToTick();
        enum class Handoff : uint8_t {
            None,    // nobody was waiting: the script side kept the world
            Yielded, // it left the world and has it again
            Closed,  // it left the world and was turned away: it no longer has it
        };
        // Between two tasks: if a tick is waiting or scripts are being evicted, leaves the world and comes back for
        // it.
        Handoff Checkpoint();

        // --- Either side ---
        bool TickWaiting() const;
        // Wakes a script side waiting for the world and turns it away from then on.
        void Close();

      private:
        enum class Owner : uint8_t {
            None,
            Tick,
            Script
        };

        mutable std::mutex _mutex;
        std::condition_variable _changed;
        Owner _owner = Owner::None;
        // Ticks waiting for the world. The script side does not take it while there are any.
        uint32_t _ticksWaiting = 0;
        uint32_t _evictions    = 0;
        // The tick that holds the world has lent it to the script side to finish its task, and takes it back when
        // the task ends. Nobody else may take the world while it is.
        bool _lent = false;
        // Between LockForScript and UnlockFromScript, also while it stopped mid-task for a tick.
        bool _scriptInTask = false;
        bool _closed       = false;
        fu2::function<void()> _contended;
    };
} // namespace Framework::Utils
