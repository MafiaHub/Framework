/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "state_bag.h"

#include "../scripting_catalog.h"

#include <core_modules.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>

#include <fmt/format.h>
#include <v8pp/convert.hpp>

#include <string>
#include <vector>

namespace Framework::Scripting::Builtins {
    namespace {
        // An unrecognised scope name is reported rather than defaulted: silently broadcasting a key
        // meant for one client is the failure this option exists to prevent.
        bool ParseScope(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> options, Networking::Replication::StateScope &out) {
            out = Networking::Replication::StateScope::Broadcast;
            if (options.IsEmpty() || options->IsNullOrUndefined()) {
                return true;
            }
            if (!options->IsObject()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "StateBag.set: options must be an object")));
                return false;
            }

            v8::Local<v8::Value> scope;
            if (!options.As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "scope")).ToLocal(&scope) || scope->IsNullOrUndefined()) {
                return true;
            }
            if (!scope->IsString()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "StateBag.set: scope must be a string")));
                return false;
            }

            const std::string name = v8pp::from_v8<std::string>(isolate, scope);
            if (name == "broadcast") {
                out = Networking::Replication::StateScope::Broadcast;
                return true;
            }
            if (name == "owner") {
                out = Networking::Replication::StateScope::Owner;
                return true;
            }
            if (name == "server") {
                out = Networking::Replication::StateScope::Server;
                return true;
            }
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, fmt::format("StateBag.set: unknown scope '{}', expected 'broadcast', 'owner' or 'server'", name))));
            return false;
        }

        bool ReadKey(const v8::FunctionCallbackInfo<v8::Value> &info, const char *method, std::string &out) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, fmt::format("StateBag.{}: expected a string key", method))));
                return false;
            }
            out = v8pp::from_v8<std::string>(isolate, info[0]);
            return true;
        }
    } // namespace

    Networking::Replication::StateBag *StateBag::ResolveFrom(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *self = v8pp::class_<StateBag>::unwrap_object(info.GetIsolate(), info.This());
        return self != nullptr ? self->Resolve() : nullptr;
    }

    Networking::Replication::StateBag *StateBag::Resolve() const {
        auto *replication = CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->GetEntityByNetworkID(_id) : nullptr;
        return entity != nullptr ? &entity->state : nullptr;
    }

    bool StateBag::ToStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> input, Networking::Replication::StateValue &out) {
        using Type = Networking::Replication::StateValue::Type;

        if (input.IsEmpty() || input->IsNullOrUndefined()) {
            out = Networking::Replication::StateValue {};
            return true;
        }
        if (input->IsBoolean()) {
            out.type    = Type::Boolean;
            out.boolean = input->BooleanValue(isolate);
            return true;
        }
        if (input->IsNumber()) {
            double number = 0.0;
            if (!input->NumberValue(context).To(&number)) {
                return false;
            }
            out.type   = Type::Number;
            out.number = number;
            return true;
        }
        if (input->IsString()) {
            out.type = Type::String;
            out.text = v8pp::from_v8<std::string>(isolate, input);
            return true;
        }

        // Everything else travels as JSON. Stringify leaves its own exception pending for a value it
        // cannot express, which is the script's error to see.
        v8::Local<v8::String> json;
        if (!v8::JSON::Stringify(context, input).ToLocal(&json)) {
            return false;
        }
        out.type = Type::Json;
        out.text = v8pp::from_v8<std::string>(isolate, json);
        return true;
    }

    v8::Local<v8::Value> StateBag::FromStateValue(v8::Isolate *isolate, v8::Local<v8::Context> context, const Networking::Replication::StateValue &value) {
        using Type = Networking::Replication::StateValue::Type;

        switch (value.type) {
        case Type::Boolean: return v8::Boolean::New(isolate, value.boolean);
        case Type::Number: return v8::Number::New(isolate, value.number);
        case Type::String: return v8pp::to_v8(isolate, value.text);
        case Type::Json: {
            // A pending exception here would surface later as an error from whatever the script was
            // actually doing. Swallow it and hand back the raw text: malformed JSON means a peer sent
            // something broken, and a read should not be what breaks a script.
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> parsed;
            if (v8::JSON::Parse(context, v8pp::to_v8(isolate, value.text)).ToLocal(&parsed)) {
                return parsed;
            }
            tryCatch.Reset();
            return v8pp::to_v8(isolate, value.text);
        }
        case Type::Null: break;
        }
        return v8::Null(isolate);
    }

    v8pp::class_<StateBag> &StateBag::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        auto &cls = _classes[isolate];
        cls =
            std::make_unique<v8pp::class_<StateBag>>(isolate, GetScriptingCatalog(isolate), "StateBag", "Arbitrary key/value state attached to one replicated entity, reached as `entity.state`. Keys set on the server replicate to every client that can currently see the entity.");
        cls->auto_wrap_objects(true);

        cls->prototype_function(
            "get",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *isolate = info.GetIsolate();
                std::string key;
                if (!ReadKey(info, "get", key)) {
                    return;
                }
                auto *bag = ResolveFrom(info);
                if (bag == nullptr) {
                    return;
                }
                const auto *value = bag->Get(key);
                if (value == nullptr) {
                    return;
                }
                info.GetReturnValue().Set(FromStateValue(isolate, isolate->GetCurrentContext(), *value));
            },
            v8pp::metadata::docs("any", {v8pp::metadata::param("key", "string", false, "Key to read.")}, "Reads one key from this entity's state.", "The stored value, or undefined when the key is not set."));

        cls->prototype_function(
            "has",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                std::string key;
                if (!ReadKey(info, "has", key)) {
                    return;
                }
                auto *bag = ResolveFrom(info);
                info.GetReturnValue().Set(bag != nullptr && bag->Has(key));
            },
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("key", "string", false, "Key to test.")}, "Checks whether this entity's state holds a key.", "True when the key is set."));

        cls->prototype_function(
            "keys",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *isolate           = info.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                auto *bag                      = ResolveFrom(info);
                if (bag == nullptr) {
                    info.GetReturnValue().Set(v8::Array::New(isolate, 0));
                    return;
                }
                const std::vector<std::string> keys = bag->Keys();
                v8::Local<v8::Array> out            = v8::Array::New(isolate, static_cast<int>(keys.size()));
                for (size_t i = 0; i < keys.size(); ++i) {
                    out->Set(context, static_cast<uint32_t>(i), v8pp::to_v8(isolate, keys[i])).Check();
                }
                info.GetReturnValue().Set(out);
            },
            v8pp::metadata::docs("string[]", {}, "Lists the keys this entity's state holds, sorted.", "Every key currently set, in ascending order."));

        cls->prototype_function(
            "toObject",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *isolate           = info.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Object> out      = v8::Object::New(isolate);
                if (auto *bag = ResolveFrom(info)) {
                    for (const std::string &key : bag->Keys()) {
                        if (const auto *value = bag->Get(key)) {
                            out->Set(context, v8pp::to_v8(isolate, key), FromStateValue(isolate, context, *value)).Check();
                        }
                    }
                }
                info.GetReturnValue().Set(out);
            },
            v8pp::metadata::docs("Record<string, any>", {}, "Copies this entity's whole state into a plain object.", "Every key and value currently set. The copy does not track later changes."));

        // Writes are the server's — the same split Entity draws around setVirtualWorld.
        if (IsClientScripting(isolate)) {
            return *cls;
        }

        cls->prototype_function(
            "set",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                v8::Isolate *isolate           = info.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                std::string key;
                if (!ReadKey(info, "set", key)) {
                    return;
                }
                if (info.Length() < 2) {
                    isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "StateBag.set: expected (key, value, options?)")));
                    return;
                }

                Networking::Replication::StateScope scope = Networking::Replication::StateScope::Broadcast;
                if (!ParseScope(isolate, context, info.Length() > 2 ? info[2] : v8::Local<v8::Value>(), scope)) {
                    return;
                }

                Networking::Replication::StateValue value;
                if (!ToStateValue(isolate, context, info[1], value)) {
                    return;
                }

                auto *bag = ResolveFrom(info);
                if (bag == nullptr) {
                    return; // Stale receiver; silent, per the builtin conventions.
                }

                const auto result = bag->Set(key, value, scope);
                if (result != Networking::Replication::StateBag::WriteResult::Applied && result != Networking::Replication::StateBag::WriteResult::Unchanged) {
                    isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, fmt::format("StateBag.set: {}", Networking::Replication::StateBag::WriteResultToString(result)))));
                    return;
                }
                info.GetReturnValue().Set(result == Networking::Replication::StateBag::WriteResult::Applied);
            },
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("key", "string", false, "Key to write; at most 64 characters."),
                    v8pp::metadata::param("value", "any", false, "Value to store. Booleans, numbers and strings travel as themselves; anything else is serialized as JSON. At most 4096 bytes."),
                    v8pp::metadata::param("options", "{ scope?: 'broadcast' | 'owner' | 'server' }", true, "Who the key reaches: every client that can see the entity (the default), only its owning client, or nobody -- server-side storage that never goes on the wire."),
                },
                "Writes one key of this entity's state and replicates it to whoever the scope names. Throws when a limit is reached.", "True when the value changed, false when it was already stored."));

        cls->prototype_function(
            "remove",
            [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                std::string key;
                if (!ReadKey(info, "remove", key)) {
                    return;
                }
                auto *bag = ResolveFrom(info);
                info.GetReturnValue().Set(bag != nullptr && bag->Remove(key));
            },
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("key", "string", false, "Key to drop.")}, "Removes one key from this entity's state, telling every client that held it.", "True when the key was set and has been removed."));

        return *cls;
    }

    v8::Local<v8::Object> StateBag::NewInstance(v8::Isolate *isolate, uint64_t networkId) {
        return GetClass(isolate).import_external(isolate, new StateBag(networkId));
    }

    void StateBag::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }
} // namespace Framework::Scripting::Builtins
