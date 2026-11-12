/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "state_bag.h"

#include "../bind/v8_callable.h"
#include "../engine_helpers.h"
#include "../lua/lua_runtime.h"
#include "../resource_monitor.h"
#include "../script_log.h"
#include "../scripting_catalog.h"
#include "../transferred_value.h"

#include "../resource/resource_manager.h"
#include "../value_transfer.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Framework::Scripting::Builtins {
    namespace {
        using Networking::Replication::StateScope;
        using Networking::Replication::StateValue;

        // An unrecognised scope name is reported rather than defaulted: silently broadcasting a key
        // meant for one client is the failure this option exists to prevent.
        bool ParseScope(Bind::Call &call, const Bind::Value &options, StateScope &out) {
            out = StateScope::Broadcast;
            if (options.IsNil()) {
                return true;
            }
            // Anything JavaScript calls an object passes, as it always did; a function or a bound object
            // has no scope to read.
            const Bind::ValueKind kind = options.Kind();
            if (kind != Bind::ValueKind::Object && kind != Bind::ValueKind::Array && kind != Bind::ValueKind::Function && kind != Bind::ValueKind::Bound) {
                call.ThrowTypeError("StateBag.set: options must be an object");
                return false;
            }

            // A getter or proxy that throws fails the read, and taking that for "no scope given" would
            // broadcast the write with an exception already pending.
            const Bind::Value scope = options.Get("scope");
            if (call.HasThrown()) {
                return false;
            }
            if (scope.IsNil()) {
                return true;
            }
            if (!scope.IsString()) {
                call.ThrowTypeError("StateBag.set: scope must be a string");
                return false;
            }

            const std::string name = scope.AsString();
            if (name == "broadcast") {
                out = StateScope::Broadcast;
                return true;
            }
            if (name == "owner") {
                out = StateScope::Owner;
                return true;
            }
            if (name == "server") {
                out = StateScope::Server;
                return true;
            }
            call.ThrowTypeError(fmt::format("StateBag.set: unknown scope '{}', expected 'broadcast', 'owner' or 'server'", name));
            return false;
        }

        bool ReadKey(Bind::Call &call, const char *method, std::string &out) {
            if (call.ArgCount() < 1 || !call.Arg(0).IsString()) {
                call.ThrowTypeError(fmt::format("StateBag.{}: expected a string key", method));
                return false;
            }
            out = call.Arg(0).AsString();
            return true;
        }

        // Scalars map directly; anything else is serialized to JSON, the same text from either language.
        // Returns false, having thrown, when the value cannot be represented — a cycle, chiefly.
        bool ReadStateValue(Bind::Call &call, const Bind::Value &input, StateValue &out) {
            using Type = StateValue::Type;

            if (input.IsNil()) {
                out = StateValue {};
                return true;
            }
            if (input.IsBoolean()) {
                out.type    = Type::Boolean;
                out.boolean = input.AsBoolean();
                return true;
            }
            if (input.IsNumber()) {
                out.type   = Type::Number;
                out.number = input.AsNumber();
                return true;
            }
            if (input.IsString()) {
                out.type = Type::String;
                out.text = input.AsString();
                return true;
            }

            auto json = input.ToJson();
            if (!json) {
                call.ThrowTypeError("StateBag.set: " + json.GetError());
                return false;
            }
            out.type = Type::Json;
            out.text = std::move(json.GetValue());
            return true;
        }

        TransferredValue Text(std::string text) {
            TransferredValue value;
            value.kind = TransferredValue::Kind::String;
            value.text = std::move(text);
            return value;
        }

        // A JSON document as the copy a handler's runtime builds its value from. Members keep the order
        // they were written in.
        TransferredValue FromJson(const nlohmann::ordered_json &json) {
            TransferredValue value;
            switch (json.type()) {
            case nlohmann::ordered_json::value_t::null: value.kind = TransferredValue::Kind::Null; break;
            case nlohmann::ordered_json::value_t::boolean:
                value.kind    = TransferredValue::Kind::Boolean;
                value.boolean = json.get<bool>();
                break;
            case nlohmann::ordered_json::value_t::number_integer:
            case nlohmann::ordered_json::value_t::number_unsigned:
            case nlohmann::ordered_json::value_t::number_float:
                value.kind   = TransferredValue::Kind::Number;
                value.number = json.get<double>();
                break;
            case nlohmann::ordered_json::value_t::string: return Text(json.get<std::string>());
            case nlohmann::ordered_json::value_t::array:
                value.kind = TransferredValue::Kind::Array;
                value.items.reserve(json.size());
                for (const auto &element : json) {
                    value.items.push_back(FromJson(element));
                }
                break;
            case nlohmann::ordered_json::value_t::object:
                value.kind = TransferredValue::Kind::Object;
                value.properties.reserve(json.size());
                for (const auto &[key, member] : json.items()) {
                    value.properties.emplace_back(key, FromJson(member));
                }
                break;
            default: break;
            }
            return value;
        }

        // A stored value as the copy any runtime builds its value from: what `get` returns and what a
        // change handler is given. A document that does not parse is its text rather than an error, so a
        // script never has to guard a read: malformed JSON means a peer sent something broken.
        TransferredValue ToTransfer(const StateValue &value) {
            using Type = StateValue::Type;

            TransferredValue out;
            switch (value.type) {
            case Type::Boolean:
                out.kind    = TransferredValue::Kind::Boolean;
                out.boolean = value.boolean;
                break;
            case Type::Number:
                out.kind   = TransferredValue::Kind::Number;
                out.number = value.number;
                break;
            case Type::String: return Text(value.text);
            case Type::Json: {
                const auto parsed = nlohmann::ordered_json::parse(value.text, nullptr, false);
                return parsed.is_discarded() ? Text(value.text) : FromJson(parsed);
            }
            case Type::Null: out.kind = TransferredValue::Kind::Null; break;
            }
            return out;
        }

        // A stored value as a script reads it: the same copy a handler is given, built in the calling
        // runtime.
        Bind::Value MakeValue(Bind::Call &call, const StateValue &value) {
            auto built = call.FromTransfer(ToTransfer(value));
            return built ? built.GetValue() : call.Nil();
        }

        // What reports a handler that failed, which the write that woke it does not hear of.
        void ReportFailure(ResourceManager *manager, const std::string &resourceName, const std::string &key, const std::string &error) {
            const std::string message = fmt::format("State change listener for '{}' failed: {}", key, error);
            // Queued, not handled here, where the script that wrote may be executing and the resource may
            // be stopping: its manager applies what the manifest asks for at its next upkeep.
            if (manager != nullptr) {
                manager->ReportHandlerFailure(resourceName, message);
                return;
            }
            LogScriptError(nullptr, resourceName, message);
        }
    } // namespace

    Networking::Replication::StateBag *StateBag::ResolveFrom(const Bind::Call &call) {
        const auto *self = call.Self<StateBag>();
        return self != nullptr ? self->Resolve() : nullptr;
    }

    Networking::Replication::StateBag *StateBag::Resolve() const {
        auto *replication = CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->GetEntityByNetworkID(_id) : nullptr;
        return entity != nullptr ? &entity->state : nullptr;
    }

    bool StateBag::ToStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> input, Networking::Replication::StateValue &out) {
        if (input.IsEmpty()) {
            out = Networking::Replication::StateValue {};
            return true;
        }

        // The conversion `set` runs reads a value of a call, so the value is handed to a function made
        // for it. What that throws stays pending here, for the caller's script to see.
        v8::HandleScope scope(isolate);
        bool converted                  = false;
        v8::Local<v8::Function> convert = Bind::MakeV8Function(isolate, context, [&out, &converted](Bind::Call &call) {
            converted = ReadStateValue(call, call.Arg(0), out);
        });
        (void)convert->Call(context, v8::Undefined(isolate), 1, &input);
        return converted;
    }

    v8::Local<v8::Value> StateBag::FromStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, const Networking::Replication::StateValue &value) {
        // A value that cannot be built, as in an isolate that is being terminated, reads as nothing
        // and leaves no exception for whatever the script was actually doing.
        v8::EscapableHandleScope scope(isolate);
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Value> built;
        if (!ValueTransfer::Rebuild(isolate, context, ToTransfer(value)).ToLocal(&built)) {
            tryCatch.Reset();
            return v8::Undefined(isolate);
        }
        return scope.Escape(built);
    }

    void StateBag::Dispatch(uint32_t id, const Networking::Replication::StateChange &change) {
        const auto entry = _subscriptions.find(id);
        if (entry == _subscriptions.end()) {
            return;
        }
        // Held: the handler may end its own subscription.
        const std::shared_ptr<Bind::ScriptCallable> callback = entry->second.callback;
        if (!callback->IsAlive()) {
            Unsubscribe(id);
            return;
        }

        // Undefined rather than null for a removal and for a key that held nothing, so a stored null
        // stays distinguishable from an absent one where the language tells the two apart.
        std::vector<TransferredValue> args;
        args.reserve(3);
        args.push_back(Text(change.key));
        args.push_back(change.removed ? TransferredValue {} : ToTransfer(change.value));
        args.push_back(change.hadPrevious ? ToTransfer(change.previous) : TransferredValue {});

        // The call is charged to the listener's resource, not the writer's. A JavaScript listener that
        // is slow is reported under its own name, as an event handler is: the monitor follows isolates,
        // and is absent off the main thread.
        v8::Isolate *isolate = callback->GetLanguage() == Language::JavaScript ? static_cast<const Bind::V8Callable &>(*callback).Isolate() : nullptr;
        auto *monitor        = isolate != nullptr ? ResourceMonitor::Current() : nullptr;
        const double charged = monitor ? monitor->ChargedMs(isolate, ResourceMonitor::Clock::now()) : 0.0;

        // A failing listener is its own bug and must not take down the write that raised it. It is
        // reported to the manager of its resource instead, when it threw and when what it waited on failed
        // later, and becomes an error of the resource at the manager's next upkeep. A listener cut short because its runtime stopped has nothing to report.
        callback->Invoke(args, Bind::ReturnMode::Outcome, [manager = entry->second.manager, resourceName = entry->second.resourceName, key = change.key](Bind::CallOutcome &&outcome) {
            if (outcome.threw && outcome.error != Bind::kRuntimeStopped) {
                ReportFailure(manager->manager, resourceName, key, outcome.error);
            }
        });

        if (monitor) {
            monitor->ReportOwnTime(isolate, "state change listener for", change.key, charged, ResourceMonitor::Clock::now());
        }
    }

    void StateBag::Unsubscribe(uint32_t id) {
        const auto it = _subscriptions.find(id);
        if (it == _subscriptions.end()) {
            return;
        }

        if (auto *replication = CoreModules::GetReplication()) {
            replication->RemoveStateChangeHandler(it->second.handle);
        }
        _subscriptions.erase(it);
    }

    void StateBag::DropSubscriptions(const std::function<bool(const Subscription &)> &drop) {
        auto *replication = CoreModules::GetReplication();
        for (auto it = _subscriptions.begin(); it != _subscriptions.end();) {
            if (!drop(it->second)) {
                ++it;
                continue;
            }
            if (replication != nullptr) {
                replication->RemoveStateChangeHandler(it->second.handle);
            }
            it = _subscriptions.erase(it);
        }
    }

    void StateBag::CleanupResource(const ResourceManager *manager, const std::string &resourceName) {
        DropSubscriptions([manager, &resourceName](const Subscription &subscription) {
            return subscription.manager->manager == manager && subscription.resourceName == resourceName;
        });
    }

    void StateBag::CleanupResource(v8::Isolate *isolate, const std::string &resourceName) {
        CleanupResource(Bind::Detail::CallResourceManager(isolate), resourceName);
    }

    void StateBag::ForgetManager(const ResourceManager *manager) {
        const auto link = _managers.find(manager);
        if (link == _managers.end()) {
            return;
        }
        // Still told apart from the subscriptions of no manager while they are dropped.
        const std::shared_ptr<ManagerLink> going = link->second;
        _managers.erase(link);
        DropSubscriptions([&going](const Subscription &subscription) {
            return subscription.manager == going;
        });
        // A handler that is still running reports to nobody.
        going->manager = nullptr;
    }

    std::shared_ptr<StateBag::ManagerLink> StateBag::LinkOf(ResourceManager *manager) {
        std::shared_ptr<ManagerLink> &link = _managers[manager];
        if (link == nullptr) {
            link          = std::make_shared<ManagerLink>();
            link->manager = manager;
        }
        return link;
    }

    void StateBag::DropDeadSubscriptions() {
        DropSubscriptions([](const Subscription &subscription) {
            return !subscription.callback->IsAlive();
        });
    }

    size_t StateBag::SubscriptionCount() {
        return _subscriptions.size();
    }

    void StateBag::Get(Bind::Call &call) {
        std::string key;
        if (!ReadKey(call, "get", key)) {
            return;
        }
        // A key that is not set, like a bag that is gone, reads as undefined, and as nil where a Lua
        // script expects the value of a call.
        auto *bag         = ResolveFrom(call);
        const auto *value = bag != nullptr ? bag->Get(key) : nullptr;
        call.Return(value != nullptr ? MakeValue(call, *value) : call.Nil());
    }

    void StateBag::Has(Bind::Call &call) {
        std::string key;
        if (!ReadKey(call, "has", key)) {
            return;
        }
        auto *bag = ResolveFrom(call);
        call.Return(bag != nullptr && bag->Has(key));
    }

    void StateBag::Keys(Bind::Call &call) {
        Bind::Value out = call.NewArray();
        if (auto *bag = ResolveFrom(call)) {
            for (const std::string &key : bag->Keys()) {
                out.Append(call.String(key));
            }
        }
        call.Return(out);
    }

    void StateBag::ToObject(Bind::Call &call) {
        auto *bag = ResolveFrom(call);
        if (bag == nullptr) {
            call.Return(call.NewObject());
            return;
        }

        // Built as one copy and not key by key: assigning "__proto__" to a JavaScript object would run the
        // inherited setter and replace its prototype, and "10" would become the tenth element of a Lua
        // table. A stored null has no entry in Lua, where a table holds no nil.
        TransferredValue copy;
        copy.kind = TransferredValue::Kind::Object;
        for (const std::string &key : bag->Keys()) {
            if (const auto *value = bag->Get(key)) {
                copy.properties.emplace_back(key, ToTransfer(*value));
            }
        }
        auto built = call.FromTransfer(copy);
        call.Return(built ? built.GetValue() : call.NewObject());
    }

    void StateBag::OnChange(Bind::Call &call) {
        // The key is required and nullable rather than optional: an optional leading parameter
        // cannot be expressed as one TypeScript signature.
        const Bind::Value keyArg = call.Arg(0);
        if (call.ArgCount() < 2 || !(keyArg.IsString() || keyArg.IsNil())) {
            call.ThrowTypeError("StateBag.onChange: expected (key, handler); pass null as the key to watch every key");
            return;
        }
        const std::string key = keyArg.IsString() ? keyArg.AsString() : std::string();

        if (!call.Arg(1).IsFunction()) {
            call.ThrowTypeError("StateBag.onChange: handler must be a function");
            return;
        }

        const auto *self = call.Self<StateBag>();
        if (self == nullptr) {
            return;
        }
        auto *replication = CoreModules::GetReplication();
        if (replication == nullptr) {
            call.ThrowError("StateBag.onChange: replication is not available");
            return;
        }

        // Owned by the resource the handler belongs to, so a stop drops it. An owner that
        // cannot be named is refused rather than stored empty: CleanupResource matches on the
        // name, so an empty one would survive every resource stop and keep calling a handler
        // whose resource is gone. Events.on refuses the same case.
        std::string resourceName = call.OwnerOf(call.Arg(1));
        if (resourceName.empty()) {
            call.ThrowError("StateBag.onChange: must be called from within a resource");
            return;
        }

        std::shared_ptr<Bind::ScriptCallable> callback = call.Arg(1).ToCallable();
        if (callback == nullptr) {
            return;
        }

        Networking::Replication::StateChangeFilter filter;
        filter.networkId = self->GetId();
        filter.key       = key;

        // Filtered on this bag's entity and, when one was named, its key -- so Dispatch never
        // runs for a change this listener did not ask for.
        const uint32_t id = ++_nextSubscriptionId;
        const auto handle = replication->AddStateChangeHandler(filter, [id](const Networking::Replication::StateChange &change) {
            Dispatch(id, change);
        });
        if (handle == Networking::Replication::kInvalidStateChangeHandle) {
            return;
        }
        _subscriptions.emplace(id, Subscription {std::move(callback), std::move(resourceName), LinkOf(call.GetResourceManager()), handle});

        // Matching what Events.on hands back.
        call.Return(call.NewFunction([id](Bind::Call &) {
            Unsubscribe(id);
        }));
    }

    void StateBag::Set(Bind::Call &call) {
        std::string key;
        if (!ReadKey(call, "set", key)) {
            return;
        }
        if (call.ArgCount() < 2) {
            call.ThrowTypeError("StateBag.set: expected (key, value, options?)");
            return;
        }

        StateScope scope = StateScope::Broadcast;
        if (!ParseScope(call, call.Arg(2), scope)) {
            return;
        }

        StateValue value;
        if (!ReadStateValue(call, call.Arg(1), value)) {
            return;
        }

        auto *bag = ResolveFrom(call);
        if (bag == nullptr) {
            return; // Stale receiver; silent, per the builtin conventions.
        }

        const auto result = bag->Set(key, value, scope);
        if (result != Networking::Replication::StateBag::WriteResult::Applied && result != Networking::Replication::StateBag::WriteResult::Unchanged) {
            call.ThrowError(fmt::format("StateBag.set: {}", Networking::Replication::StateBag::WriteResultToString(result)));
            return;
        }
        call.Return(result == Networking::Replication::StateBag::WriteResult::Applied);
    }

    void StateBag::Remove(Bind::Call &call) {
        std::string key;
        if (!ReadKey(call, "remove", key)) {
            return;
        }
        auto *bag = ResolveFrom(call);
        call.Return(bag != nullptr && bag->Remove(key));
    }

    const Bind::Class<StateBag> &StateBag::Definition() {
        using Bind::Doc;
        using Bind::TypeOf;
        namespace Type = Bind::Type;

        static const auto definition =
            Bind::Class<StateBag>("StateBag", "Arbitrary key/value state attached to one replicated entity, reached as `entity.state`. Keys set on the server replicate to every client that can currently see the entity.")
                .Method("get", &StateBag::Get, Doc("Reads one key from this entity's state.").Param("key", TypeOf<std::string>(), "Key to read.").ReturnType(Type::Any()).Returns("The stored value, or undefined when the key is not set."))
                .Method("has", &StateBag::Has, Doc("Checks whether this entity's state holds a key.").Param("key", TypeOf<std::string>(), "Key to test.").ReturnType(TypeOf<bool>()).Returns("True when the key is set."))
                .Method("keys", &StateBag::Keys, Doc("Lists the keys this entity's state holds, sorted.").ReturnType(TypeOf<std::vector<std::string>>()).Returns("Every key currently set, in ascending order."))
                .Method("toObject", &StateBag::ToObject, Doc("Copies this entity's whole state into a plain object.").ReturnType(Type::Custom("Record<string, any>", "table<string, any>")).Returns("Every key and value currently set. The copy does not track later changes."))
                .Method("onChange", &StateBag::OnChange,
                    Doc("Watches this entity's state. The filter is applied before the handler runs, so a listener watching one key of one entity is not woken by unrelated changes. The subscription is dropped when the registering resource stops.")
                        .Param("key", Type::Custom("string | null", "string?"), "Only report this key, or null to report every key of this entity.")
                        .Param("handler", Type::Custom("Function", "fun(key: string, value: any, previous: any)"),
                            "Called as (key: string, value: any, previous: any) with the changed key, its new value and the value before it. `value` is undefined when the key was removed and `previous` is undefined when it held nothing.")
                        .ReturnType(Type::Custom("Function", "fun()"))
                        .Returns("A zero-argument function that cancels this subscription."))
                // Writes are the server's — the same split Entity draws around setVirtualWorld.
                .Method("set", &StateBag::Set,
                    Doc("Writes one key of this entity's state and replicates it to whoever the scope names. Throws when a limit is reached.")
                        .Param("key", TypeOf<std::string>(), "Key to write; at most 64 UTF-8 bytes.")
                        .Param("value", Type::Any(), "Value to store. Booleans, numbers and strings travel as themselves; anything else is serialized as JSON. At most 4096 bytes.")
                        .OptionalParam("options", Type::Custom("{ scope?: 'broadcast' | 'owner' | 'server' }", "{ scope?: 'broadcast'|'owner'|'server' }"),
                            "Who the key reaches: every client that can see the entity (the default), only its owning client, or nobody -- server-side storage that never goes on the wire.")
                        .ReturnType(TypeOf<bool>())
                        .Returns("True when the value changed, false when it was already stored."),
                    Bind::Side::Server)
                .Method("remove", &StateBag::Remove, Doc("Removes one key from this entity's state, telling every client that held it.").Param("key", TypeOf<std::string>(), "Key to drop.").ReturnType(TypeOf<bool>()).Returns("True when the key was set and has been removed."),
                    Bind::Side::Server)
                // Crosses between runtimes as the id of its entity.
                .Transfer(&StateBag::GetId);
        return definition;
    }

    v8pp::class_<StateBag> &StateBag::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    v8::Local<v8::Object> StateBag::NewInstance(v8::Isolate *isolate, uint64_t networkId) {
        return GetClass(isolate).import_external(isolate, new StateBag(networkId));
    }

    void StateBag::UnregisterIsolate(v8::Isolate *isolate) {
        // A handler of the isolate is dead already when the isolate was told to go ahead of this
        // (Builtins::UnregisterAll), and alive under its name otherwise.
        DropSubscriptions([isolate](const Subscription &subscription) {
            return !subscription.callback->IsAlive() || (subscription.callback->GetLanguage() == Language::JavaScript && static_cast<const Bind::V8Callable &>(*subscription.callback).Isolate() == isolate);
        });
        _classes.Unregister(isolate);
    }

    void StateBag::RegisterLua(LuaRuntime &runtime) {
        Definition().MaterialiseLua(runtime.State(), runtime.Options().side);
    }
} // namespace Framework::Scripting::Builtins
