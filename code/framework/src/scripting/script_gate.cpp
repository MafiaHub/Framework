/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "script_gate.h"

#include <algorithm>
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
            // Calls made back into script by native code that script called, since the outermost call into the
            // isolate began: while one is in progress that native code is on the stack, so this is no safe point.
            // V8 reports when such a call starts and never when it returns, so each is remembered by where it was
            // made from: how deep the script's stack was, and which call in which script was at the top of it. It
            // has returned once the stack is no deeper than that, or that place on the stack holds something else.
            // The script thread's alone.
            struct NestedCall {
                int depth    = 0;
                int scriptId = 0;
                int line     = 0;
                int column   = 0;
            };
            static constexpr int kMaxNestedCalls = 8;
            NestedCall nested[kMaxNestedCalls];
            int nestedCount = 0;
            // More of them than are remembered, or made from deeper than is looked at: no safe point until the
            // outermost call returns.
            bool untracked = false;
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

        // Frames of script looked at; a stack this deep or deeper is not told apart.
        constexpr int kMaxTrackedDepth = 48;

        v8::Local<v8::StackTrace> ScriptStack(v8::Isolate *isolate, int limit) {
            return v8::StackTrace::CurrentStackTrace(isolate, limit, static_cast<v8::StackTrace::StackTraceOptions>(v8::StackTrace::kLineNumber | v8::StackTrace::kColumnOffset | v8::StackTrace::kScriptId));
        }

        // Whether script of this isolate is running further up this thread's stack. The caller is inside the isolate.
        bool ScriptOnStack(v8::Isolate *isolate) {
            v8::HandleScope scope(isolate);
            return v8::StackTrace::CurrentStackTrace(isolate, 1, v8::StackTrace::kOverview)->GetFrameCount() > 0;
        }

        // Whether native code that called back into script may still be on the stack. Forgets the calls it finds
        // have returned.
        bool UnderNestedCall(IsolateState &state) {
            if (state.untracked) {
                return true;
            }
            if (state.nestedCount == 0) {
                return false;
            }
            v8::Isolate *isolate = state.isolate;
            v8::HandleScope scope(isolate);
            const v8::Local<v8::StackTrace> stack = ScriptStack(isolate, kMaxTrackedDepth);
            const int depth                       = stack->GetFrameCount();
            if (depth >= kMaxTrackedDepth) {
                return true;
            }
            for (int i = 0; i < state.nestedCount; ++i) {
                const IsolateState::NestedCall &call = state.nested[i];
                // Deeper than where the call was made from: in progress if the frame that made it is still there
                // and still at that call. Frames come top first.
                if (depth > call.depth) {
                    const v8::Local<v8::StackFrame> frame = stack->GetFrame(isolate, static_cast<uint32_t>(depth - call.depth));
                    if (frame->GetScriptId() == call.scriptId && frame->GetLineNumber() == call.line && frame->GetColumn() == call.column) {
                        return true;
                    }
                }
            }
            state.nestedCount = 0;
            return false;
        }

        void Request(IsolateState &state) {
            if (!state.requested.exchange(true, std::memory_order_acq_rel)) {
                state.isolate->RequestInterrupt(
                    [](v8::Isolate *, void *data) {
                        auto *self = static_cast<IsolateState *>(data);
                        self->requested.store(false, std::memory_order_release);
                        ScriptGate *gate = gGate.load(std::memory_order_acquire);
                        // Held off: the tick goes on waiting and asks again (it does while it waits), by when what
                        // holds this off may have ended.
                        if (gate != nullptr && gate->OnScriptThread() && tHeldOff == 0 && gate->SafePointWanted() && !UnderNestedCall(*self)) {
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
            if (state == nullptr || state->untracked) {
                return;
            }
            v8::HandleScope scope(isolate);
            const v8::Local<v8::StackTrace> stack = ScriptStack(isolate, kMaxTrackedDepth);
            const int depth                       = stack->GetFrameCount();
            if (depth == 0) {
                Rearm(*state);
                return;
            }
            if (depth >= kMaxTrackedDepth || state->nestedCount == IsolateState::kMaxNestedCalls) {
                state->untracked = true;
                return;
            }
            const v8::Local<v8::StackFrame> top = stack->GetFrame(isolate, 0);
            const IsolateState::NestedCall call {depth, top->GetScriptId(), top->GetLineNumber(), top->GetColumn()};
            // The same call site again, as a native that calls back once per element does: already remembered.
            for (int i = 0; i < state->nestedCount; ++i) {
                const IsolateState::NestedCall &known = state->nested[i];
                if (known.depth == call.depth && known.scriptId == call.scriptId && known.line == call.line && known.column == call.column) {
                    return;
                }
            }
            state->nested[state->nestedCount++] = call;
        }

        // The outermost call into the isolate returned: nothing native is left under script there.
        void OnCallCompleted(v8::Isolate *isolate) {
            IsolateState *state = Find(isolate);
            if (state != nullptr && (state->nestedCount != 0 || state->untracked)) {
                state->nestedCount = 0;
                state->untracked   = false;
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

    RuntimeEntry::RuntimeEntry(Into into) {
        ScriptGate *gate = gGate.load(std::memory_order_acquire);
        if (gate == nullptr) {
            return;
        }
        if (!gate->OnScriptThread()) {
            if (into == Into::OwnIsolate) {
                return;
            }
            // The script thread finishes its task before this returns, and may need an isolate this thread has
            // locked to do it: let go of it for the wait. Locals made in it stay valid.
            v8::Isolate *held = v8::Isolate::TryGetCurrent();
            if (held != nullptr && v8::Locker::IsLocked(held)) {
                const v8::Unlocker unlocked(held);
                gate->EnterFromOutside();
            }
            else {
                gate->EnterFromOutside();
            }
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
