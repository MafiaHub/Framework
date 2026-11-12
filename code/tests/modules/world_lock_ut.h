/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/world_lock.h"

#include <atomic>
#include <chrono>
#include <thread>

MODULE(world_lock, {
    using Framework::Utils::WorldLock;
    using namespace std::chrono_literals;

    // Spins until `flag`, for at most a few seconds.
    const auto waitFor = [](const std::atomic<bool> &flag) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!flag && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(100us);
        }
        return flag.load();
    };

    IT("gives the tick the world at once while scripts are idle, without asking them to yield", {
        WorldLock lock;
        int asked = 0;
        lock.SetContendedHook([&] {
            ++asked;
        });
        lock.LockForTick();
        lock.UnlockFromTick();
        EQUALS(asked, 0);
        EQUALS(lock.TickWaiting(), false);
    });

    IT("does not make a script yield when no tick is waiting", {
        WorldLock lock;
        EQUALS(lock.LockForScript(), true);
        EQUALS(lock.YieldToTick(), false);
        EQUALS(lock.Checkpoint() == WorldLock::Handoff::None, true);
        lock.UnlockFromScript();
    });

    IT("hands the world to a waiting tick at the script's next yield, and back when the tick is done", {
        WorldLock lock;
        std::atomic<bool> asked {false}, scriptInTask {false}, tickDone {false}, stop {false};
        std::atomic<int> yields {0};
        // What the two sides share: only ever touched by the side that holds the world.
        int world              = 0;
        bool scriptSawTickWork = false;
        lock.SetContendedHook([&] {
            asked = true;
        });

        std::thread script([&] {
            lock.LockForScript();
            scriptInTask = true;
            // One long task, as a script that never returns to its loop: it only reaches safe points.
            while (!stop) {
                const int before = world;
                if (lock.YieldToTick()) {
                    ++yields;
                    scriptSawTickWork = scriptSawTickWork || world == before + 1000;
                }
            }
            lock.UnlockFromScript();
        });
        EQUALS(waitFor(scriptInTask), true);

        lock.LockForTick();
        // The script is still inside its task, stopped at a safe point: the world is the tick's.
        world += 1000;
        tickDone = true;
        lock.UnlockFromTick();

        while (yields.load() == 0) {
            std::this_thread::sleep_for(100us);
        }
        stop = true;
        script.join();
        EQUALS(asked.load(), true);
        EQUALS(yields.load(), 1);
        EQUALS(scriptSawTickWork, true);
    });

    IT("never lets both sides into the world at once", {
        WorldLock lock;
        std::atomic<bool> stop {false};
        std::atomic<int> inside {0};
        std::atomic<bool> overlapped {false};
        long scriptWork = 0, tickWork = 0;
        const auto touch = [&](long &mine) {
            if (inside.fetch_add(1) != 0) {
                overlapped = true;
            }
            ++mine;
            inside.fetch_sub(1);
        };

        std::thread script([&] {
            while (!stop) {
                if (!lock.LockForScript()) {
                    return;
                }
                for (int i = 0; i < 200 && !stop; ++i) {
                    touch(scriptWork);
                    // Leaves the world for the length of a tick, then is back inside it.
                    lock.YieldToTick();
                    lock.Checkpoint();
                }
                lock.UnlockFromScript();
            }
        });
        for (int tick = 0; tick < 2000; ++tick) {
            lock.LockForTick();
            touch(tickWork);
            lock.UnlockFromTick();
            // A tick is followed by a wait for the next one; ticks back to back would keep scripts out, by design.
            std::this_thread::sleep_for(20us);
        }
        stop = true;
        script.join();
        EQUALS(overlapped.load(), false);
        EQUALS(tickWork, 2000L);
        GREATER(scriptWork, 0L);
    });

    IT("lets a waiting tick go before a script that comes back for the world", {
        WorldLock lock;
        std::atomic<bool> scriptHolds {false}, release {false}, scriptBack {false};
        std::atomic<bool> tickRan {false};
        bool tickRanBeforeScriptCameBack = false;

        std::thread script([&] {
            lock.LockForScript();
            scriptHolds = true;
            while (!release) {
                std::this_thread::sleep_for(100us);
            }
            // Between two tasks: the tick that has been waiting goes first.
            lock.Checkpoint();
            tickRanBeforeScriptCameBack = tickRan.load();
            scriptBack                  = true;
            lock.UnlockFromScript();
        });
        EQUALS(waitFor(scriptHolds), true);
        std::thread tick([&] {
            lock.LockForTick();
            tickRan = true;
            lock.UnlockFromTick();
        });
        while (!lock.TickWaiting()) {
            std::this_thread::sleep_for(100us);
        }
        release = true;
        tick.join();
        script.join();
        EQUALS(tickRanBeforeScriptCameBack, true);
    });

    IT("puts a script that is mid-task out of the world before the tick enters scripts itself", {
        WorldLock lock;
        std::atomic<bool> scriptInTask {false}, taskFinished {false}, stop {false};
        std::atomic<bool> scriptReentered {false};

        std::thread script([&] {
            lock.LockForScript();
            scriptInTask = true;
            // Mid-task until told to stop; yields to the tick on the way.
            while (!stop) {
                lock.YieldToTick();
            }
            taskFinished = true;
            lock.UnlockFromScript();
            // The next task has to wait for the tick to be done with scripts and with the world.
            if (lock.LockForScript()) {
                scriptReentered = true;
                lock.UnlockFromScript();
            }
        });
        EQUALS(waitFor(scriptInTask), true);

        lock.LockForTick();
        std::thread finisher([&] {
            std::this_thread::sleep_for(20ms);
            stop = true;
        });
        // Returns only once the script has left its task: nothing of it is on a stack mid-call.
        lock.EvictScript();
        EQUALS(taskFinished.load(), true);
        std::this_thread::sleep_for(20ms);
        EQUALS(scriptReentered.load(), false);
        lock.ReadmitScript();
        std::this_thread::sleep_for(20ms);
        // Readmitted, but the tick still holds the world.
        EQUALS(scriptReentered.load(), false);
        lock.UnlockFromTick();
        finisher.join();
        script.join();
        EQUALS(scriptReentered.load(), true);
    });

    IT("evicts at once a script that is not in a task", {
        WorldLock lock;
        lock.LockForTick();
        lock.EvictScript();
        lock.EvictScript();
        lock.ReadmitScript();
        lock.ReadmitScript();
        lock.UnlockFromTick();
        EQUALS(lock.LockForScript(), true);
        lock.UnlockFromScript();
    });

    IT("turns a script away once closed, also one already waiting", {
        WorldLock lock;
        lock.LockForTick();
        std::atomic<bool> returned {false};
        bool got = true;
        std::thread script([&] {
            got      = lock.LockForScript();
            returned = true;
        });
        std::this_thread::sleep_for(20ms);
        EQUALS(returned.load(), false);
        lock.Close();
        EQUALS(waitFor(returned), true);
        script.join();
        EQUALS(got, false);
        EQUALS(lock.LockForScript(), false);
        lock.UnlockFromTick();
    });

    IT("keeps asking a script for a safe point for as long as the tick waits", {
        WorldLock lock;
        std::atomic<int> asked {0};
        std::atomic<bool> scriptInTask {false};
        lock.SetContendedHook([&] {
            ++asked;
        });
        std::thread script([&] {
            lock.LockForScript();
            scriptInTask = true;
            // Cannot stop when first asked: something of its own is in the way for a while.
            while (asked.load() < 5) {
                std::this_thread::sleep_for(100us);
            }
            lock.YieldToTick();
            lock.UnlockFromScript();
        });
        EQUALS(waitFor(scriptInTask), true);
        lock.LockForTick();
        lock.UnlockFromTick();
        script.join();
        GREATEREQ(asked.load(), 5);
    });

    IT("lends the world to the script for an eviction, and to nobody else", {
        // Who gets the world when it is lent is a race, so the race is run many times: one win by the wrong
        // thread is a failure.
        int intrusions = 0;
        int rounds     = 0;
        for (int round = 0; round < 300; ++round) {
            WorldLock lock;
            std::atomic<bool> scriptInTask {false}, stop {false};
            // Raised by the tick for as long as it is in the middle of its work, the eviction included.
            std::atomic<bool> tickBusy {false};
            std::atomic<bool> intruded {false};

            std::thread script([&] {
                lock.LockForScript();
                scriptInTask = true;
                while (!stop) {
                    lock.YieldToTick();
                }
                lock.UnlockFromScript();
            });
            if (!waitFor(scriptInTask)) {
                stop = true;
                script.join();
                continue;
            }

            lock.LockForTick();
            tickBusy = true;
            // A second thread on the tick side, such as a web worker entering scripts, wants the world too.
            std::thread other([&] {
                lock.LockForTick();
                if (tickBusy) {
                    intruded = true;
                }
                lock.UnlockFromTick();
            });
            while (!lock.TickWaiting()) {
                std::this_thread::yield();
            }
            std::thread finisher([&] {
                std::this_thread::sleep_for(200us);
                stop = true;
            });
            lock.EvictScript();
            lock.ReadmitScript();
            tickBusy = false;
            lock.UnlockFromTick();
            finisher.join();
            other.join();
            script.join();
            intrusions += intruded ? 1 : 0;
            ++rounds;
        }
        EQUALS(intrusions, 0);
        GREATER(rounds, 250);
    });
});
