/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_callable.h"

#include "call.h"
#include "lua_class.h"

#include "../lua/lua_engine.h"
#include "../lua/lua_runtime.h"
#include "../lua/lua_scheduler.h"
#include "../lua/lua_transfer.h"
#include "../lua/lua_value_walk.h"

#include <lauxlib.h>
#include <lua.h>

#include <climits>
#include <map>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// The Lua side of ScriptCallable and Deferred.
//
// What is kept for a runtime is one Link, which the registry of its state holds and every callable and deferred
// result of it shares. It is how they reach the runtime, and how they are told that it is gone: nothing here keeps
// a LuaRuntime that it could touch after the fact.
//
// A callable holds its function through a reference in the registry. The registry also maps a function to the id of
// its callable, which is how the same function is found to have one already. A function made to stand for a
// callable of another runtime is a closure over that callable.
//
// Outside a script's call the state is touched with raw access only, on the main thread, and with nothing that
// can raise outside a protected call.

namespace Framework::Scripting::Bind {
    namespace {
        class Function;

        // Not const, so that no other object can come to share their addresses.
        char linkKey      = 0;
        char callablesKey = 0;
        char standInKey   = 0;
        char standInsKey  = 0;

        struct Link final {
            // nullptr once the runtime is going.
            LuaRuntime *runtime = nullptr;
            // By the id the registry has for the function. A callable takes itself out as it goes.
            std::unordered_map<uint64_t, std::weak_ptr<Function>> callables;
            // The `done` of every Invoke whose coroutine has not ended.
            std::map<uint64_t, Completion> pending;
            uint64_t lastId = 0;

            // The runtime while script can still be started and resumed in it: not once it was destroyed from
            // inside its own call, where its state outlives it until the engine ticks.
            LuaRuntime *Running() const {
                return runtime != nullptr && !runtime->Scheduler().IsStopped() ? runtime : nullptr;
            }
        };

        CallOutcome Failed(std::string error) {
            CallOutcome outcome;
            outcome.threw = true;
            outcome.error = std::move(error);
            return outcome;
        }

        // The runtime is going: every call it still owed is told so.
        void Stop(const std::shared_ptr<Link> &link) {
            if (link->runtime == nullptr) {
                return;
            }
            // Dead before anything is told: what `done` invokes in this runtime is answered at once.
            link->runtime = nullptr;
            link->callables.clear();
            auto pending = std::move(link->pending);
            link->pending.clear();
            for (auto &[key, done] : pending) {
                done(Failed(kRuntimeStopped));
            }
        }

        // __gc of what the registry holds the link in, which runs as the state closes: for a runtime that
        // nobody spoke for. The registry is left saying that the link is gone, and the userdata without its
        // metatable, so that nothing a later finalizer does reaches the link through either. Putting a value in a
        // field the registry has allocates nothing.
        int CollectLink(lua_State *L) {
            auto *held = static_cast<std::shared_ptr<Link> *>(lua_touserdata(L, 1));
            Stop(*held);
            held->~shared_ptr();
            lua_pushnil(L);
            lua_setmetatable(L, 1);
            lua_pushboolean(L, 0);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &linkKey);
            return 0;
        }

        // The link of the runtime of `L`, or nullptr when nothing was kept for it yet. `closing` is told whether
        // the state had one and is closing. Raises nothing.
        std::shared_ptr<Link> FindLink(lua_State *L, bool *closing = nullptr) {
            if (lua_checkstack(L, 1) == 0) {
                return nullptr;
            }
            std::shared_ptr<Link> link;
            const int kept = lua_rawgetp(L, LUA_REGISTRYINDEX, &linkKey);
            if (kept == LUA_TUSERDATA) {
                link = *static_cast<std::shared_ptr<Link> *>(lua_touserdata(L, -1));
            }
            else if (closing != nullptr) {
                *closing = kept == LUA_TBOOLEAN;
            }
            lua_pop(L, 1);
            return link;
        }

        // The link of the runtime of `L`, made when it has none. In a state that is closing it is one that is
        // dead and that nothing keeps: a state finalizes nothing it is given that late.
        std::shared_ptr<Link> LinkOf(lua_State *L) {
            bool closing = false;
            if (std::shared_ptr<Link> link = FindLink(L, &closing)) {
                return link;
            }
            if (closing) {
                return std::make_shared<Link>();
            }
            luaL_checkstack(L, 3, "cannot keep a function");
            void *block = lua_newuserdatauv(L, sizeof(std::shared_ptr<Link>), 0);
            lua_createtable(L, 0, 2);
            lua_pushcfunction(L, CollectLink);
            lua_setfield(L, -2, "__gc");
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");
            // Built once nothing above can raise any more, and finalized from the moment it has its metatable.
            auto link     = std::make_shared<Link>();
            link->runtime = LuaRuntime::From(L);
            ::new (block) std::shared_ptr<Link>(link);
            lua_setmetatable(L, -2);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &linkKey);
            return link;
        }

        // Pushes the table of the registry that maps a function to the id of its callable. Its keys are weak: it
        // keeps no function alive.
        void PushCallables(lua_State *L) {
            if (lua_rawgetp(L, LUA_REGISTRYINDEX, &callablesKey) == LUA_TTABLE) {
                return;
            }
            lua_pop(L, 1);
            lua_createtable(L, 0, 0);
            lua_createtable(L, 0, 1);
            lua_pushstring(L, "k");
            lua_setfield(L, -2, "__mode");
            lua_setmetatable(L, -2);
            lua_pushvalue(L, -1);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &callablesKey);
        }

        // Pushes the table of the registry that maps a callable of another runtime, by its address, to the function
        // that stands for it here. Its values are weak, so an entry goes when the script lets go of the function,
        // and that is before the function lets go of the callable: no address in it is ever stale.
        void PushStandIns(lua_State *L) {
            if (lua_rawgetp(L, LUA_REGISTRYINDEX, &standInsKey) == LUA_TTABLE) {
                return;
            }
            lua_pop(L, 1);
            lua_createtable(L, 0, 0);
            lua_createtable(L, 0, 1);
            lua_pushstring(L, "v");
            lua_setfield(L, -2, "__mode");
            lua_setmetatable(L, -2);
            lua_pushvalue(L, -1);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &standInsKey);
        }

        // What a call whose coroutine ended with its first result on top of `thread` tells its caller.
        CallOutcome CopyResult(LuaRuntime &runtime, lua_State *thread, ReturnMode mode) {
            CallOutcome outcome;
            if (mode == ReturnMode::Outcome) {
                if (lua_type(thread, -1) == LUA_TBOOLEAN) {
                    outcome.value.kind    = TransferredValue::Kind::Boolean;
                    outcome.value.boolean = lua_toboolean(thread, -1) != 0;
                }
                return outcome;
            }
            // Copied on the main thread: a coroutine that has ended is not one to call a function on.
            lua_State *L = runtime.State();
            if (lua_checkstack(L, 1) == 0 || lua_checkstack(thread, 1) == 0) {
                return Failed("cannot grow the Lua stack");
            }
            lua_pushvalue(thread, -1);
            lua_xmove(thread, L, 1);
            auto copied = LuaTransfer::Copy(L, -1);
            lua_pop(L, 1);
            if (!copied) {
                return Failed(copied.GetError());
            }
            outcome.value = std::move(copied).GetValue();
            return outcome;
        }

        // The `done` of the coroutine of an Invoke.
        void Complete(const std::shared_ptr<Link> &link, uint64_t key, ReturnMode mode, bool ok, lua_State *thread, const std::string &error) {
            const auto found = link->pending.find(key);
            if (found == link->pending.end()) {
                return;
            }
            // Taken out first: `done` may invoke callables and destroy runtimes.
            Completion done = std::move(found->second);
            link->pending.erase(found);
            done(ok ? CopyResult(*link->runtime, thread, mode) : Failed(error));
        }

        class Function final: public ScriptCallable {
          public:
            Function(std::shared_ptr<Link> link, uint64_t id, std::string resource): _link(std::move(link)), _id(id), _resource(std::move(resource)) {}

            // Lets go of the function while its state is open. Nothing in this can raise: a field is cleared and a
            // reference given back, neither of which allocates.
            ~Function() override {
                _link->callables.erase(_id);
                LuaRuntime *runtime = _link->runtime;
                if (runtime == nullptr || _reference == LUA_NOREF) {
                    return;
                }
                lua_State *L = runtime->State();
                if (lua_checkstack(L, 3) == 0) {
                    return;
                }
                if (lua_rawgetp(L, LUA_REGISTRYINDEX, &callablesKey) == LUA_TTABLE) {
                    lua_rawgeti(L, LUA_REGISTRYINDEX, _reference);
                    lua_pushvalue(L, -1);
                    const bool own = lua_rawget(L, -3) == LUA_TNUMBER && static_cast<uint64_t>(lua_tointegerx(L, -1, nullptr)) == _id;
                    lua_pop(L, 1);
                    if (own) {
                        lua_pushnil(L);
                        lua_rawset(L, -3);
                    }
                    else {
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
                luaL_unref(L, LUA_REGISTRYINDEX, _reference);
            }

            // Takes the function on top of the stack. Raises when Lua is out of memory.
            void Hold(lua_State *L) {
                _reference = luaL_ref(L, LUA_REGISTRYINDEX);
            }

            // Pushes the function when `L` is of the runtime that owns it and that runtime is not gone.
            bool PushIn(lua_State *L) const {
                if (_link->runtime == nullptr || _link->runtime != LuaRuntime::From(L) || _reference == LUA_NOREF) {
                    return false;
                }
                lua_rawgeti(L, LUA_REGISTRYINDEX, _reference);
                return true;
            }

            Language GetLanguage() const override {
                return Language::Lua;
            }

            std::string ResourceName() const override {
                return _resource;
            }

            bool IsAlive() const override {
                return _link->Running() != nullptr;
            }

            // A function has one callable while anyone holds it.
            bool Equals(const ScriptCallable &other) const override {
                return this == &other;
            }

            void Invoke(const std::vector<TransferredValue> &args, ReturnMode mode, Completion done) override {
                if (!done) {
                    done = [](CallOutcome &&) {};
                }
                // Held: the function, and `done`, may drop the callable.
                const std::shared_ptr<Link> link = _link;
                LuaRuntime *runtime              = link->Running();
                if (runtime == nullptr) {
                    done(Failed(kRuntimeStopped));
                    return;
                }
                lua_State *L  = runtime->State();
                const int top = lua_gettop(L);
                if (args.size() > static_cast<size_t>(INT_MAX - 2) || lua_checkstack(L, static_cast<int>(args.size()) + 2) == 0) {
                    done(Failed("too many arguments"));
                    return;
                }
                lua_rawgeti(L, LUA_REGISTRYINDEX, _reference);
                for (size_t i = 0; i < args.size(); ++i) {
                    // Protected, each of them: what cannot be built raises nothing here.
                    std::string refused;
                    if (!LuaTransfer::Rebuild(L, args[i], refused)) {
                        lua_settop(L, top);
                        done(Failed("Argument " + std::to_string(i + 1) + ": " + refused));
                        return;
                    }
                }

                const uint64_t key = ++link->lastId;
                link->pending.emplace(key, std::move(done));
                runtime->Scheduler().Start(static_cast<int>(args.size()), [link, key, mode](bool ok, lua_State *thread, const std::string &error) {
                    Complete(link, key, mode, ok, thread, error);
                });
            }

          private:
            const std::shared_ptr<Link> _link;
            const uint64_t _id;
            const std::string _resource;
            int _reference = LUA_NOREF;
        };

        // What a function that stands for a callable of another runtime closes over.
        struct StandIn final {
            std::shared_ptr<ScriptCallable> callable;
            NativeFunction forward;
        };

        static_assert(alignof(StandIn) <= alignof(Detail::LuaMaxAlign), "a StandIn fits the alignment of a userdata");

        // __gc of a StandIn. The userdata loses its metatable with it, which is how a call that still reaches it
        // tells.
        int CollectStandIn(lua_State *L) {
            static_cast<StandIn *>(lua_touserdata(L, 1))->~StandIn();
            lua_pushnil(L);
            lua_setmetatable(L, 1);
            return 0;
        }

        int CallStandIn(lua_State *L) {
            luaL_checkstack(L, 1, nullptr);
            if (lua_getmetatable(L, lua_upvalueindex(1)) == 0) {
                return luaL_error(L, "function called while its state closes");
            }
            lua_pop(L, 1);
            return Detail::LuaInvokeNative(L, static_cast<StandIn *>(lua_touserdata(L, lua_upvalueindex(1)))->forward, false);
        }

        // The callable the function at `index` stands for, or nullptr when it is a function of the script.
        std::shared_ptr<ScriptCallable> StoodFor(lua_State *L, int index) {
            if (lua_tocfunction(L, index) != &CallStandIn || lua_getupvalue(L, index, 1) == nullptr) {
                return nullptr;
            }
            std::shared_ptr<ScriptCallable> callable;
            if (lua_getmetatable(L, -1) != 0) {
                lua_pop(L, 1);
                callable = static_cast<StandIn *>(lua_touserdata(L, -1))->callable;
            }
            lua_pop(L, 1);
            return callable;
        }

        // A value that waits in the registry for the coroutine it is resumed with, and leaves it when that
        // coroutine is never resumed.
        class Parked final {
          public:
            Parked(std::shared_ptr<Link> link, int reference): _link(std::move(link)), _reference(reference) {}
            Parked(Parked &&other) noexcept: _link(std::move(other._link)), _reference(std::exchange(other._reference, LUA_NOREF)) {}
            Parked(const Parked &)            = delete;
            Parked &operator=(const Parked &) = delete;
            Parked &operator=(Parked &&)      = delete;

            // Giving a reference back allocates nothing, so nothing in this can raise.
            ~Parked() {
                if (_reference != LUA_NOREF && _link->runtime != nullptr && lua_checkstack(_link->runtime->State(), 2) != 0) {
                    luaL_unref(_link->runtime->State(), LUA_REGISTRYINDEX, _reference);
                }
            }

            int Push(lua_State *thread) {
                lua_rawgeti(thread, LUA_REGISTRYINDEX, _reference);
                luaL_unref(thread, LUA_REGISTRYINDEX, std::exchange(_reference, LUA_NOREF));
                return 1;
            }

          private:
            std::shared_ptr<Link> _link;
            int _reference;
        };

        class DeferredResult final: public Detail::LuaDeferred {
          public:
            explicit DeferredResult(std::shared_ptr<Link> link): _link(std::move(link)) {}

            int Finish(lua_State *L, const char *label, std::optional<std::string> &error) override {
                _returned = true;
                if (Settled()) {
                    std::string refused;
                    if (_error) {
                        error = std::move(*_error);
                    }
                    else if (!LuaTransfer::Rebuild(L, _value, refused)) {
                        error = std::move(refused);
                    }
                    _value = {};
                    return error ? 0 : 1;
                }
                // What the function left on the stack is of no use while the coroutine waits.
                lua_settop(L, 0);
                _token = LuaRuntime::From(L)->Scheduler().Suspend(L, label);
                return LuaScheduler::Wait(L);
            }

          protected:
            void OnResolve(TransferredValue &&value) override {
                if (!_returned) {
                    _value = std::move(value);
                    return;
                }
                LuaRuntime *runtime = _link->Running();
                if (runtime == nullptr || _token == 0) {
                    return;
                }
                // Built here, where what Lua cannot be given is still an error the script can catch, and kept in
                // the registry until the coroutine takes it: the stack of a coroutine that waits is not one to
                // build on.
                lua_State *L = runtime->State();
                std::string refused;
                int reference = LUA_NOREF;
                if (LuaTransfer::Rebuild(L, value, refused)) {
                    const bool parked = LuaValues::Protected(
                        L,
                        [L, &reference]() {
                            lua_pushvalue(L, 2);
                            reference = luaL_ref(L, LUA_REGISTRYINDEX);
                            return true;
                        },
                        lua_gettop(L), false, refused);
                    lua_pop(L, 1);
                    if (!parked) {
                        reference = LUA_NOREF;
                    }
                }
                if (reference == LUA_NOREF) {
                    runtime->Scheduler().ResumeWithError(std::exchange(_token, 0), refused);
                    return;
                }
                runtime->Scheduler().Resume(std::exchange(_token, 0), [parked = Parked(_link, reference)](lua_State *thread) mutable {
                    return parked.Push(thread);
                });
            }

            void OnReject(std::string &&message) override {
                if (!_returned) {
                    _error = std::move(message);
                    return;
                }
                LuaRuntime *runtime = _link->Running();
                if (runtime != nullptr && _token != 0) {
                    runtime->Scheduler().ResumeWithError(std::exchange(_token, 0), message);
                }
            }

          private:
            const std::shared_ptr<Link> _link;
            // Whether the function has returned. Until then a settlement is kept for the call to give at once.
            bool _returned = false;
            TransferredValue _value;
            std::optional<std::string> _error;
            // What the coroutine that waits is resumed with.
            uint64_t _token = 0;
        };
    } // namespace

    std::shared_ptr<ScriptCallable> LuaToCallable(lua_State *L, int index) {
        if (lua_type(L, index) != LUA_TFUNCTION) {
            return nullptr;
        }
        index = lua_absindex(L, index);
        luaL_checkstack(L, 4, "cannot keep a function");

        // A function that stands for a callable is that callable to whoever keeps it or sends it on, so that it
        // comes home as itself and is not wrapped again on the way.
        if (std::shared_ptr<ScriptCallable> original = StoodFor(L, index)) {
            return original;
        }

        const std::shared_ptr<Link> link = LinkOf(L);
        const std::string &resource      = LuaRuntime::From(L)->Options().resourceName;
        // Of a runtime that is going the callable is dead from the start, and holds nothing.
        if (link->runtime == nullptr) {
            return std::make_shared<Function>(link, 0, resource);
        }

        PushCallables(L);
        lua_pushvalue(L, index);
        if (lua_rawget(L, -2) == LUA_TNUMBER) {
            const auto found = link->callables.find(static_cast<uint64_t>(lua_tointegerx(L, -1, nullptr)));
            if (found != link->callables.end()) {
                if (std::shared_ptr<Function> existing = found->second.lock()) {
                    lua_pop(L, 2);
                    return existing;
                }
            }
        }
        lua_pop(L, 1);

        // What raises below unwinds the callable, which gives back what it took.
        const uint64_t id = ++link->lastId;
        auto callable     = std::make_shared<Function>(link, id, resource);
        lua_pushvalue(L, index);
        callable->Hold(L);
        lua_pushvalue(L, index);
        lua_pushinteger(L, static_cast<lua_Integer>(id));
        lua_rawset(L, -3);
        lua_pop(L, 1);
        link->callables[id] = callable;
        return callable;
    }

    void LuaPushCallable(lua_State *L, const std::shared_ptr<ScriptCallable> &callable) {
        luaL_checkstack(L, 5, "cannot push a function");
        // A function coming home is itself.
        if (const auto *own = dynamic_cast<const Function *>(callable.get()); own != nullptr && own->PushIn(L)) {
            return;
        }

        // The function that stands for it here already: it arrives as the same function each time.
        PushStandIns(L);
        if (lua_rawgetp(L, -1, callable.get()) == LUA_TFUNCTION) {
            lua_remove(L, -2);
            return;
        }
        lua_pop(L, 1);

        void *block = lua_newuserdatauv(L, sizeof(StandIn), 0);
        if (lua_rawgetp(L, LUA_REGISTRYINDEX, &standInKey) == LUA_TNIL) {
            lua_pop(L, 1);
            lua_createtable(L, 0, 2);
            lua_pushcfunction(L, CollectStandIn);
            lua_setfield(L, -2, "__gc");
            lua_pushboolean(L, 0);
            lua_setfield(L, -2, "__metatable");
            lua_pushvalue(L, -1);
            lua_rawsetp(L, LUA_REGISTRYINDEX, &standInKey);
        }
        // Built once nothing above can raise any more, and finalized from the moment it has its metatable.
        ::new (block) StandIn {callable, Detail::CallableForwarder(callable)};
        lua_setmetatable(L, -2);
        lua_pushcclosure(L, CallStandIn, 1);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, -3, callable.get());
        lua_remove(L, -2);
    }

    void LuaRuntimeDisposing(LuaRuntime &runtime) {
        if (const std::shared_ptr<Link> link = FindLink(runtime.State())) {
            Stop(link);
        }
    }

    size_t LuaCallableCount(LuaRuntime &runtime) {
        const std::shared_ptr<Link> link = FindLink(runtime.State());
        return link != nullptr ? link->callables.size() : 0;
    }

    void WatchRuntimes(LuaEngine &engine) {
        engine.AddRuntimeDisposingListener([](LuaRuntime &runtime) {
            LuaRuntimeDisposing(runtime);
        });
    }

    namespace Detail {
        std::shared_ptr<LuaDeferred> LuaDefer(lua_State *L) {
            return std::make_shared<DeferredResult>(LinkOf(L));
        }
    } // namespace Detail
} // namespace Framework::Scripting::Bind
