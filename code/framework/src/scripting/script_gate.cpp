/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "script_gate.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Framework::Scripting {
    namespace {
        std::atomic<ScriptGate *> gGate {nullptr};

        struct IsolateState {
            v8::Isolate *isolate = nullptr;
            // An interrupt was queued in the isolate since the script thread last started running it: one at a time,
            // or an isolate that runs nothing for an hour would collect one per contended tick. Not "is pending":
            // V8 keeps the pending flag with the thread that holds the isolate's lock, and one raised while nobody
            // does is forgotten when the lock is next taken. So the script thread clears this each time it starts
            // running the isolate (OnIsolateRunning), and asks again there if the tick is still waiting.
            std::atomic<bool> requested {false};
            // Native code that script called has called back into script, and that call has not returned as far as
            // can be told: the native code is on the stack, so this is no safe point. The script thread's alone.
            bool nestedCall = false;
        };

        std::mutex gIsolatesMutex;
        std::unordered_map<v8::Isolate *, std::unique_ptr<IsolateState>> gIsolates;
        // States of isolates that left. Kept: an interrupt queued just before can still fire while the isolate runs
        // its last script, and it is handed the state. A few bytes per runtime ever started.
        std::vector<std::unique_ptr<IsolateState>> gRetired;
        // Bumped when an isolate leaves, so a thread's remembered lookup is not trusted past it.
        std::atomic<uint64_t> gIsolatesGeneration {1};

        // NoSafePoint and RuntimeEntry in force on this thread.
        thread_local uint32_t tHeldOff = 0;

        IsolateState *Find(v8::Isolate *isolate) {
            thread_local v8::Isolate *lastIsolate = nullptr;
            thread_local IsolateState *lastState  = nullptr;
            thread_local uint64_t lastGeneration  = 0;
            const uint64_t generation             = gIsolatesGeneration.load(std::memory_order_acquire);
            if (lastIsolate == isolate && lastGeneration == generation) {
                return lastState;
            }
            std::lock_guard lock(gIsolatesMutex);
            const auto it  = gIsolates.find(isolate);
            lastIsolate    = isolate;
            lastState      = it != gIsolates.end() ? it->second.get() : nullptr;
            lastGeneration = generation;
            return lastState;
        }

        // Whether script of this isolate is running further up this thread's stack. The caller is inside the isolate.
        bool ScriptOnStack(v8::Isolate *isolate) {
            v8::HandleScope scope(isolate);
            return v8::StackTrace::CurrentStackTrace(isolate, 1)->GetFrameCount() > 0;
        }

        void Request(IsolateState &state) {
            if (!state.requested.exchange(true, std::memory_order_acq_rel)) {
                state.isolate->RequestInterrupt(
                    [](v8::Isolate *, void *data) {
                        auto *self = static_cast<IsolateState *>(data);
                        self->requested.store(false, std::memory_order_release);
                        ScriptGate *gate = gGate.load(std::memory_order_acquire);
                        // Held off: the tick goes on waiting, and is asked for again when what holds it off ends.
                        if (gate != nullptr && gate->OnScriptThread() && tHeldOff == 0 && !self->nestedCall && gate->SafePointWanted()) {
                            gate->OnSafePoint();
                        }
                    },
                    &state);
            }
        }

        // What held a safe point off is over: if the tick is still waiting, the script is asked again, since the
        // interrupt that found it held off is spent.
        void AskAgain(IsolateState *state) {
            ScriptGate *gate = gGate.load(std::memory_order_acquire);
            if (gate == nullptr || !gate->OnScriptThread() || !gate->SafePointWanted()) {
                return;
            }
            if (state != nullptr) {
                Request(*state);
            }
            else {
                RequestSafePoint();
            }
        }

        // The script thread holds the isolate's lock and is about to run script in it: a request made before this
        // may be lost, one made from here on is not.
        void Rearm(IsolateState &state) {
            state.requested.store(false, std::memory_order_release);
            ScriptGate *gate = gGate.load(std::memory_order_acquire);
            if (gate != nullptr && gate->OnScriptThread() && gate->SafePointWanted()) {
                Request(state);
            }
        }

        // Every call the API makes into script. One made while script is already running came from native code
        // that script called; any other is the script thread starting to run the isolate.
        void OnBeforeCall(v8::Isolate *isolate) {
            if (gGate.load(std::memory_order_acquire) == nullptr) {
                return;
            }
            IsolateState *state = Find(isolate);
            if (state == nullptr || state->nestedCall) {
                return;
            }
            if (ScriptOnStack(isolate)) {
                state->nestedCall = true;
            }
            else {
                Rearm(*state);
            }
        }

        // The outermost call into the isolate returned: nothing native is left under script there.
        void OnCallCompleted(v8::Isolate *isolate) {
            IsolateState *state = Find(isolate);
            if (state != nullptr && state->nestedCall) {
                state->nestedCall = false;
                AskAgain(state);
            }
        }
    } // namespace

    void ScriptGate::Install(ScriptGate *gate) {
        gGate.store(gate, std::memory_order_release);
    }

    ScriptGate *ScriptGate::Get() {
        return gGate.load(std::memory_order_acquire);
    }

    RuntimeEntry::RuntimeEntry() {
        ScriptGate *gate = gGate.load(std::memory_order_acquire);
        if (gate == nullptr) {
            return;
        }
        if (!gate->OnScriptThread()) {
            gate->EnterFromOutside();
            _outside = gate;
            return;
        }
        // Already held off: nothing to find out. Otherwise script further up the stack means native code brought us
        // here, from whichever isolate this thread is in now.
        v8::Isolate *current = v8::Isolate::TryGetCurrent();
        if (tHeldOff != 0 || (current != nullptr && current->InContext() && ScriptOnStack(current))) {
            ++tHeldOff;
            _holdsOff = true;
        }
    }

    RuntimeEntry::~RuntimeEntry() {
        if (_outside != nullptr) {
            _outside->LeaveFromOutside();
        }
        else if (_holdsOff && --tHeldOff == 0) {
            AskAgain(nullptr);
        }
    }

    NoSafePoint::NoSafePoint() {
        ++tHeldOff;
    }

    NoSafePoint::~NoSafePoint() {
        if (--tHeldOff == 0) {
            AskAgain(nullptr);
        }
    }

    void RegisterIsolate(v8::Isolate *isolate) {
        auto state     = std::make_unique<IsolateState>();
        state->isolate = isolate;
        {
            std::lock_guard lock(gIsolatesMutex);
            gIsolates[isolate] = std::move(state);
        }
        gIsolatesGeneration.fetch_add(1, std::memory_order_acq_rel);
        isolate->AddBeforeCallEnteredCallback(&OnBeforeCall);
        isolate->AddCallCompletedCallback(&OnCallCompleted);
    }

    void UnregisterIsolate(v8::Isolate *isolate) {
        isolate->RemoveBeforeCallEnteredCallback(&OnBeforeCall);
        isolate->RemoveCallCompletedCallback(&OnCallCompleted);
        {
            std::lock_guard lock(gIsolatesMutex);
            if (const auto it = gIsolates.find(isolate); it != gIsolates.end()) {
                // Spent: a request that finds it set queues nothing more on an isolate about to go.
                it->second->requested.store(true, std::memory_order_release);
                gRetired.push_back(std::move(it->second));
                gIsolates.erase(it);
            }
        }
        gIsolatesGeneration.fetch_add(1, std::memory_order_acq_rel);
    }

    void OnIsolateRunning(v8::Isolate *isolate) {
        if (gGate.load(std::memory_order_acquire) == nullptr) {
            return;
        }
        if (IsolateState *state = Find(isolate)) {
            Rearm(*state);
        }
    }

    void RequestSafePoint() {
        std::lock_guard lock(gIsolatesMutex);

        for (const auto &[isolate, state] : gIsolates) {
            Request(*state);
        }
    }
} // namespace Framework::Scripting
