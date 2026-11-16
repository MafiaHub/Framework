/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

#include <function2/function2.hpp>

namespace Framework::Scripting {
    /**
     * What the scripting layer needs from a host that runs scripts on a thread of their own, beside the thread
     * that owns the world (the server's tick thread). With none installed, scripts run on the thread that ticks
     * the engine and everything here does nothing.
     *
     * Two things cross between the threads:
     *
     * - The tick thread wants the world while a script is running. It asks for a safe point (RequestSafePoint):
     *   V8 then calls back between two instructions of the script, and the host parks the script thread there for
     *   the length of the tick. A safe point is only taken where nothing but script is on the script thread's
     *   stack: not while a native function further up is in the middle of its work (see NoSafePoint and the nested
     *   call tracking in RegisterIsolate). The tick asks again for as long as it waits, so it gets in as soon as
     *   that native function has returned.
     *
     * - A thread that is not the script thread raises an event, the way server code always has: an EngineScope
     *   or V8's own scopes on the engine's isolate, values built there, and an emit. None of that runs script on
     *   the calling thread any more. Off the script thread the engine's isolate is one of that side's own
     *   (NodeEngine's off-thread runtime), which the script thread never enters, so the two never wait on each
     *   other's lock; the emit copies its arguments and posts the dispatch to the script thread (Post), or runs it
     *   there and waits for the answer when the caller needs one (Run).
     *
     * - What is left is a thread that has to go into the script side's runtimes itself: starting or stopping a
     *   resource, calling a resource's function directly. RuntimeEntry makes that safe: the script thread is
     *   first put out of whatever it was running, so two threads are never inside those runtimes at once. That
     *   waits for the script thread's current task.
     */
    class ScriptGate {
      public:
        virtual ~ScriptGate() = default;

        virtual bool OnScriptThread() const = 0;
        // A thread other than the script thread enters the runtimes, and leaves. Calls nest.
        virtual void EnterFromOutside() = 0;
        virtual void LeaveFromOutside() = 0;
        // Runs a task on the script thread, with the world; Run waits for it, and a caller that has the world
        // gives it up meanwhile. How anything that ends in script is handed over from another thread.
        virtual void Post(fu2::unique_function<void()> task) = 0;
        virtual void Run(fu2::unique_function<void()> task)  = 0;
        // Whether the tick thread is waiting for the script thread to reach a safe point.
        virtual bool SafePointWanted() const = 0;
        // The script thread is at one. Returns when the script may carry on.
        virtual void OnSafePoint() = 0;

        // Null removes it. Not owned.
        static void Install(ScriptGate *gate);
        static ScriptGate *Get();
    };

    /**
     * Opens every way into a runtime that does not start in the runtime's own loop: EngineScope, a call from one
     * runtime into another, a host script. First thing in the function, before any V8 scope.
     *
     * Off the script thread it puts the script thread out for its lifetime. On the script thread, when script is
     * already running further up the stack, it holds safe points off: the native function that got here from that
     * script is in the middle of its work.
     */
    class RuntimeEntry final {
      public:
        enum class Into {
            // A runtime the script thread runs: another thread has to put the script thread out first.
            ScriptRuntimes,
            // The engine's isolate as the calling thread sees it, which off the script thread is that side's own:
            // nothing to put out.
            OwnIsolate,
        };

        explicit RuntimeEntry(Into into = Into::ScriptRuntimes);
        ~RuntimeEntry();

        RuntimeEntry(const RuntimeEntry &)            = delete;
        RuntimeEntry &operator=(const RuntimeEntry &) = delete;

      private:
        ScriptGate *_outside = nullptr;
        bool _holdsOff       = false;
    };

    /**
     * No safe point is taken on this thread while one is alive. For native code that calls into script, or does
     * anything else a tick may run under, while it holds on to something the tick thread may free or move.
     */
    class NoSafePoint final {
      public:
        NoSafePoint();
        ~NoSafePoint();

        NoSafePoint(const NoSafePoint &)            = delete;
        NoSafePoint &operator=(const NoSafePoint &) = delete;
    };

    /**
     * Makes an isolate take part: it can be asked for a safe point, and calls made into it from native code that
     * script called are noticed, so no safe point is taken under them. V8 does not say when such a call returns,
     * so it is told from the stack: the call is over once the script is no deeper than where it made it, or the
     * place it was made from is no longer on the stack at that call. From the thread that runs the isolate,
     * before it runs script and after it last did.
     */
    void RegisterIsolate(v8::Isolate *isolate);
    void UnregisterIsolate(v8::Isolate *isolate);

    // The calling thread holds the isolate's lock and is about to run its loop: script that runs without a call
    // from native code announcing it (a promise continuation) can then be asked for a safe point too.
    void OnIsolateRunning(v8::Isolate *isolate);

    // Asks every registered isolate for a safe point at its next instruction boundary. Any thread.
    void RequestSafePoint();

    /**
     * How the engine's run loop gives the world up while it waits and between the runtimes it runs. Release and
     * Acquire bracket a wait; Checkpoint sits between two runtimes. Acquire and Checkpoint return false when the
     * script thread is being shut down and no longer has the world: the loop returns at once.
     */
    class RunGate {
      public:
        virtual ~RunGate()        = default;
        virtual void Release()    = 0;
        virtual bool Acquire()    = 0;
        virtual bool Checkpoint() = 0;
    };
} // namespace Framework::Scripting
