/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/script_gate.h>
#include <utils/task_scheduler.h>
#include <utils/world_lock.h>

#include <function2/function2.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace Framework::Integrations::Server {
    /**
     * The thread a server's scripts run on, beside the tick thread.
     *
     * Every runtime is created, run and freed here, so a script that computes for a long time holds up the other
     * scripts and nothing else: the tick thread takes the world from it at its next safe point (see
     * Utils::WorldLock and Scripting::ScriptGate) and replication carries on.
     *
     * This thread has the world whenever it runs anything, so what runs here -- a script's native call, a task
     * posted from the tick thread -- may touch the world as tick-thread code does, and is never run while a tick is.
     */
    class ScriptThread final
        : public Framework::Scripting::ScriptGate
        , public Framework::Scripting::RunGate {
      public:
        using Task = Utils::TaskScheduler::Task;

        struct Hooks {
            // Once per tick interval, with the world: what the tick used to do for scripts.
            fu2::function<void()> round;
            // Run the runtimes that have work until the time given, giving the world up while waiting (the engine
            // does, through this object as its RunGate). False when there is no engine to run yet.
            fu2::function<bool(std::chrono::steady_clock::time_point)> runUntil;
        };

        explicit ScriptThread(double tickIntervalSeconds);
        ~ScriptThread() override;

        void Start(Hooks hooks);
        // Lets the task in progress finish, drops the ones still queued, and joins. A thread that has the world
        // gives it up while it waits.
        void Stop();

        // --- Any thread ---
        // Runs `task` on the script thread, with the world.
        void Post(Task task);
        // The same, and waits for it. A thread that has the world gives it up while it waits, so the world may have
        // changed when this returns. On the script thread, or with the thread stopped, runs it at once.
        void Run(Task task);

        // --- The tick side: any thread but the script thread, one at a time ---
        void LockWorld();
        void UnlockWorld();
        // Whether the calling thread took the world with LockWorld.
        static bool HoldsWorld();

        // --- ScriptGate ---
        bool OnScriptThread() const override;
        void EnterFromOutside() override;
        void LeaveFromOutside() override;
        bool SafePointWanted() const override;
        void OnSafePoint() override;

        // --- RunGate, for the engine on this thread ---
        void Release() override;
        bool Acquire() override;
        bool Checkpoint() override;

      private:
        void Loop();

        Utils::WorldLock _world;
        // Only a queue here: no background workers. Drained by the script thread.
        Utils::TaskScheduler _tasks {0};
        Hooks _hooks;
        std::chrono::steady_clock::duration _tickInterval;
        std::thread _thread;
        std::atomic<std::thread::id> _threadId {};
        std::atomic<bool> _running {false};
        std::atomic<bool> _stop {false};
        // The script thread's own: whether it has the world right now.
        bool _hasWorld = false;
    };
} // namespace Framework::Integrations::Server
