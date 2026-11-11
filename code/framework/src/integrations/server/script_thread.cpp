/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "script_thread.h"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace Framework::Integrations::Server {
    namespace {
        // How long the script thread runs scripts before it looks at its task queue again: what a tick-thread
        // event waits, at most, before its handlers start.
        constexpr auto kTaskLatency = std::chrono::milliseconds(2);

        // Whether this thread took the world as the tick side, and how deep it is inside runtimes it entered from
        // outside the script thread.
        thread_local bool tHoldsWorld       = false;
        thread_local uint32_t tOutside      = 0;
        thread_local bool tTookWorldToEnter = false;
    } // namespace

    ScriptThread::ScriptThread(double tickIntervalSeconds): _tickInterval(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(tickIntervalSeconds > 0.0 ? tickIntervalSeconds : 1.0 / 60.0))) {
        _world.SetContendedHook([] {
            Framework::Scripting::RequestSafePoint();
        });
    }

    ScriptThread::~ScriptThread() {
        Stop();
    }

    void ScriptThread::Start(Hooks hooks) {
        _hooks   = std::move(hooks);
        _running = true;
        _thread  = std::thread([this] {
            _threadId = std::this_thread::get_id();
            Loop();
        });
        // Installed once the id is known, or the first entry from this thread would count itself as the script thread.
        while (_threadId.load() == std::thread::id {}) {
            std::this_thread::yield();
        }
        Framework::Scripting::ScriptGate::Install(this);
    }

    void ScriptThread::Stop() {
        if (!_running.exchange(false)) {
            return;
        }
        // A script stopped mid-task for this thread's tick needs the world back to finish its task.
        const bool had = tHoldsWorld;
        if (had) {
            UnlockWorld();
        }
        _stop = true;
        _world.Close();
        if (_thread.joinable()) {
            _thread.join();
        }
        Framework::Scripting::ScriptGate::Install(nullptr);
        if (had) {
            LockWorld();
        }
    }

    void ScriptThread::Post(Task task) {
        _tasks.PostToOwner([this, task = std::move(task)]() mutable {
            // Turned away between two tasks: this one would run without the world.
            if (!_hasWorld) {
                return;
            }
            task();
            // Before the next task is started, not after it is destroyed: what it captured goes first.
            task = nullptr;
            Checkpoint();
        });
    }

    void ScriptThread::Run(Task task) {
        if (!_running || OnScriptThread()) {
            task();
            return;
        }
        struct Done {
            std::mutex mutex;
            std::condition_variable settled;
            bool done = false;
        };
        const auto done = std::make_shared<Done>();
        const bool had  = tHoldsWorld;
        if (had) {
            UnlockWorld();
        }
        // Settled when the task is destroyed, so a task that is dropped unrun (the thread was stopped) does not
        // leave this waiting.
        struct Settle {
            explicit Settle(std::shared_ptr<Done> state): done(std::move(state)) {}
            Settle(const Settle &)            = delete;
            Settle &operator=(const Settle &) = delete;

            std::shared_ptr<Done> done;
            ~Settle() {
                {
                    std::lock_guard lock(done->mutex);
                    done->done = true;
                }
                done->settled.notify_all();
            }
        };
        Post([task = std::move(task), settle = std::make_unique<Settle>(done)]() mutable {
            task();
        });
        {
            std::unique_lock lock(done->mutex);
            done->settled.wait(lock, [&] {
                return done->done;
            });
        }
        if (had) {
            LockWorld();
        }
    }

    void ScriptThread::LockWorld() {
        _world.LockForTick();
        tHoldsWorld = true;
    }

    void ScriptThread::UnlockWorld() {
        tHoldsWorld = false;
        _world.UnlockFromTick();
    }

    bool ScriptThread::HoldsWorld() {
        return tHoldsWorld;
    }

    bool ScriptThread::OnScriptThread() const {
        return std::this_thread::get_id() == _threadId.load(std::memory_order_relaxed);
    }

    void ScriptThread::EnterFromOutside() {
        if (tOutside++ == 0) {
            tTookWorldToEnter = !tHoldsWorld;
            if (tTookWorldToEnter) {
                LockWorld();
            }
        }
        _world.EvictScript();
    }

    void ScriptThread::LeaveFromOutside() {
        _world.ReadmitScript();
        if (--tOutside == 0 && tTookWorldToEnter) {
            tTookWorldToEnter = false;
            UnlockWorld();
        }
    }

    bool ScriptThread::SafePointWanted() const {
        return _world.TickWaiting();
    }

    void ScriptThread::OnSafePoint() {
        _world.YieldToTick();
    }

    void ScriptThread::Release() {
        if (_hasWorld) {
            _hasWorld = false;
            _world.UnlockFromScript();
        }
    }

    bool ScriptThread::Acquire() {
        if (!_hasWorld) {
            _hasWorld = _world.LockForScript();
        }
        return _hasWorld;
    }

    bool ScriptThread::Checkpoint() {
        if (_hasWorld && _world.Checkpoint() == Utils::WorldLock::Handoff::Closed) {
            _hasWorld = false;
        }
        return _hasWorld;
    }

    void ScriptThread::Loop() {
        auto nextRound = std::chrono::steady_clock::now();
        while (!_stop && Acquire()) {
            _tasks.Drain();
            if (!_hasWorld) {
                break;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextRound) {
                if (_hooks.round) {
                    _hooks.round();
                }
                // From now, not from when it was due: a round that ran long is not followed by a burst of them.
                nextRound = std::chrono::steady_clock::now() + _tickInterval;
            }
            if (!Checkpoint()) {
                break;
            }
            const auto until = std::min(nextRound, std::chrono::steady_clock::now() + kTaskLatency);
            if (!_hooks.runUntil || !_hooks.runUntil(until)) {
                Release();
                std::this_thread::sleep_until(until);
            }
        }
        Release();
    }
} // namespace Framework::Integrations::Server
