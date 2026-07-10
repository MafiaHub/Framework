/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "discord.h"

#include "integrations/client/instance.h"

#include "core_modules.h"

#include <v8pp/convert.hpp>

#include <discord.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        // Staged activity mutated by the setters, published on update()/setPresence().
        discord::Activity _activity;
        std::mutex _mutex;

        enum class StringField {
            Name,
            Details,
            State,
            LargeImage,
            LargeText,
            SmallImage,
            SmallText,
            PartyId,
            MatchSecret,
            JoinSecret,
            SpectateSecret,
        };

        enum class NumberField {
            StartTimestamp,
            EndTimestamp,
            SupportedPlatforms,
        };

        External::Discord::Wrapper *ResolvePresence() {
            auto *instance = CoreModules::GetClientInstance();
            return instance ? instance->GetPresence() : nullptr;
        }

        void ThrowError(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        discord::ActivityType MapActivityType(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
            if (value->IsNumber()) {
                return static_cast<discord::ActivityType>(value->Int32Value(context).FromMaybe(0));
            }
            if (value->IsString()) {
                const std::string name = ToLower(v8pp::from_v8<std::string>(isolate, value));
                if (name == "streaming") {
                    return discord::ActivityType::Streaming;
                }
                if (name == "listening") {
                    return discord::ActivityType::Listening;
                }
                if (name == "watching") {
                    return discord::ActivityType::Watching;
                }
            }
            return discord::ActivityType::Playing;
        }

        discord::ActivityPartyPrivacy MapPrivacy(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
            if (value->IsNumber()) {
                return static_cast<discord::ActivityPartyPrivacy>(value->Int32Value(context).FromMaybe(0));
            }
            if (value->IsString() && ToLower(v8pp::from_v8<std::string>(isolate, value)) == "public") {
                return discord::ActivityPartyPrivacy::Public;
            }
            return discord::ActivityPartyPrivacy::Private;
        }

        // Apply*/Read* require the caller to hold _mutex.
        void ApplyString(StringField field, const std::string &value) {
            const char *c = value.c_str();
            switch (field) {
            case StringField::Name: _activity.SetName(c); break;
            case StringField::Details: _activity.SetDetails(c); break;
            case StringField::State: _activity.SetState(c); break;
            case StringField::LargeImage: _activity.GetAssets().SetLargeImage(c); break;
            case StringField::LargeText: _activity.GetAssets().SetLargeText(c); break;
            case StringField::SmallImage: _activity.GetAssets().SetSmallImage(c); break;
            case StringField::SmallText: _activity.GetAssets().SetSmallText(c); break;
            case StringField::PartyId: _activity.GetParty().SetId(c); break;
            case StringField::MatchSecret: _activity.GetSecrets().SetMatch(c); break;
            case StringField::JoinSecret: _activity.GetSecrets().SetJoin(c); break;
            case StringField::SpectateSecret: _activity.GetSecrets().SetSpectate(c); break;
            }
        }

        void ApplyNumber(NumberField field, std::int64_t value) {
            switch (field) {
            case NumberField::StartTimestamp: _activity.GetTimestamps().SetStart(static_cast<discord::Timestamp>(value)); break;
            case NumberField::EndTimestamp: _activity.GetTimestamps().SetEnd(static_cast<discord::Timestamp>(value)); break;
            case NumberField::SupportedPlatforms: _activity.SetSupportedPlatforms(static_cast<std::uint32_t>(value)); break;
            }
        }

        bool Publish() {
            auto *presence = ResolvePresence();
            if (!presence) {
                return false;
            }
            discord::Activity snapshot;
            {
                std::scoped_lock lock(_mutex);
                snapshot = _activity;
            }
            return presence->UpdateActivity(snapshot).IsOk();
        }

        bool ReadString(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> opts, const char *key, StringField field) {
            v8::Local<v8::Value> value;
            if (opts->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value) && value->IsString()) {
                ApplyString(field, v8pp::from_v8<std::string>(isolate, value));
                return true;
            }
            return false;
        }

        bool ReadNumber(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> opts, const char *key, NumberField field) {
            v8::Local<v8::Value> value;
            if (opts->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value) && value->IsNumber()) {
                ApplyNumber(field, value->IntegerValue(context).FromMaybe(0));
                return true;
            }
            return false;
        }
    } // anonymous namespace

    void Discord::SetStringFieldCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowError(isolate, "Discord: expected a string argument");
            return;
        }
        const auto field = static_cast<StringField>(args.Data()->Int32Value(isolate->GetCurrentContext()).FromMaybe(0));
        std::scoped_lock lock(_mutex);
        ApplyString(field, v8pp::from_v8<std::string>(isolate, args[0]));
    }

    void Discord::SetNumberFieldCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            ThrowError(isolate, "Discord: expected a numeric argument");
            return;
        }
        const auto field = static_cast<NumberField>(args.Data()->Int32Value(context).FromMaybe(0));
        std::scoped_lock lock(_mutex);
        ApplyNumber(field, args[0]->IntegerValue(context).FromMaybe(0));
    }

    void Discord::SetTypeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1) {
            ThrowError(isolate, "Discord.setType: expected (type)");
            return;
        }
        const discord::ActivityType type = MapActivityType(isolate, context, args[0]);
        std::scoped_lock lock(_mutex);
        _activity.SetType(type);
    }

    void Discord::SetPartySizeCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
            ThrowError(isolate, "Discord.setPartySize: expected (current, max)");
            return;
        }
        const std::int32_t current = args[0]->Int32Value(context).FromMaybe(0);
        const std::int32_t max     = args[1]->Int32Value(context).FromMaybe(0);
        std::scoped_lock lock(_mutex);
        _activity.GetParty().GetSize().SetCurrentSize(current);
        _activity.GetParty().GetSize().SetMaxSize(max);
    }

    void Discord::SetPartyPrivacyCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1) {
            ThrowError(isolate, "Discord.setPartyPrivacy: expected (privacy)");
            return;
        }
        const discord::ActivityPartyPrivacy privacy = MapPrivacy(isolate, context, args[0]);
        std::scoped_lock lock(_mutex);
        _activity.GetParty().SetPrivacy(privacy);
    }

    void Discord::SetInstanceCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1) {
            ThrowError(isolate, "Discord.setInstance: expected (bool)");
            return;
        }
        const bool instance = args[0]->BooleanValue(isolate);
        std::scoped_lock lock(_mutex);
        _activity.SetInstance(instance);
    }

    void Discord::SetAssetsCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1 || !args[0]->IsObject()) {
            ThrowError(isolate, "Discord.setAssets: expected an object");
            return;
        }
        v8::Local<v8::Object> opts = args[0].As<v8::Object>();
        std::scoped_lock lock(_mutex);
        ReadString(isolate, context, opts, "largeImage", StringField::LargeImage);
        ReadString(isolate, context, opts, "largeText", StringField::LargeText);
        ReadString(isolate, context, opts, "smallImage", StringField::SmallImage);
        ReadString(isolate, context, opts, "smallText", StringField::SmallText);
    }

    void Discord::SetPartyCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1 || !args[0]->IsObject()) {
            ThrowError(isolate, "Discord.setParty: expected an object");
            return;
        }
        v8::Local<v8::Object> opts = args[0].As<v8::Object>();
        std::scoped_lock lock(_mutex);
        ReadString(isolate, context, opts, "id", StringField::PartyId);

        // size: [current, max]
        v8::Local<v8::Value> sizeVal;
        if (opts->Get(context, v8pp::to_v8(isolate, "size")).ToLocal(&sizeVal) && sizeVal->IsArray()) {
            v8::Local<v8::Array> arr = sizeVal.As<v8::Array>();
            if (arr->Length() >= 2) {
                v8::Local<v8::Value> cur;
                v8::Local<v8::Value> max;
                if (arr->Get(context, 0).ToLocal(&cur) && arr->Get(context, 1).ToLocal(&max)) {
                    _activity.GetParty().GetSize().SetCurrentSize(cur->Int32Value(context).FromMaybe(0));
                    _activity.GetParty().GetSize().SetMaxSize(max->Int32Value(context).FromMaybe(0));
                }
            }
        }

        v8::Local<v8::Value> privacyVal;
        if (opts->Get(context, v8pp::to_v8(isolate, "privacy")).ToLocal(&privacyVal) && !privacyVal->IsUndefined()) {
            _activity.GetParty().SetPrivacy(MapPrivacy(isolate, context, privacyVal));
        }
    }

    void Discord::SetSecretsCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1 || !args[0]->IsObject()) {
            ThrowError(isolate, "Discord.setSecrets: expected an object");
            return;
        }
        v8::Local<v8::Object> opts = args[0].As<v8::Object>();
        std::scoped_lock lock(_mutex);
        ReadString(isolate, context, opts, "match", StringField::MatchSecret);
        ReadString(isolate, context, opts, "join", StringField::JoinSecret);
        ReadString(isolate, context, opts, "spectate", StringField::SpectateSecret);
    }

    void Discord::SetPresenceCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 1 || !args[0]->IsObject()) {
            ThrowError(isolate, "Discord.setPresence: expected an options object");
            return;
        }
        v8::Local<v8::Object> opts = args[0].As<v8::Object>();

        {
            std::scoped_lock lock(_mutex);

            v8::Local<v8::Value> typeVal;
            if (opts->Get(context, v8pp::to_v8(isolate, "type")).ToLocal(&typeVal) && !typeVal->IsUndefined()) {
                _activity.SetType(MapActivityType(isolate, context, typeVal));
            }

            ReadString(isolate, context, opts, "name", StringField::Name);
            ReadString(isolate, context, opts, "details", StringField::Details);
            ReadString(isolate, context, opts, "state", StringField::State);

            ReadNumber(isolate, context, opts, "startTimestamp", NumberField::StartTimestamp);
            ReadNumber(isolate, context, opts, "endTimestamp", NumberField::EndTimestamp);
            ReadNumber(isolate, context, opts, "supportedPlatforms", NumberField::SupportedPlatforms);

            ReadString(isolate, context, opts, "largeImage", StringField::LargeImage);
            ReadString(isolate, context, opts, "largeText", StringField::LargeText);
            ReadString(isolate, context, opts, "smallImage", StringField::SmallImage);
            ReadString(isolate, context, opts, "smallText", StringField::SmallText);

            v8::Local<v8::Value> instanceVal;
            if (opts->Get(context, v8pp::to_v8(isolate, "instance")).ToLocal(&instanceVal) && instanceVal->IsBoolean()) {
                _activity.SetInstance(instanceVal->BooleanValue(isolate));
            }

            // Nested party: { id?, size?: [current, max], privacy? }
            v8::Local<v8::Value> partyVal;
            if (opts->Get(context, v8pp::to_v8(isolate, "party")).ToLocal(&partyVal) && partyVal->IsObject()) {
                v8::Local<v8::Object> party = partyVal.As<v8::Object>();
                ReadString(isolate, context, party, "id", StringField::PartyId);
                v8::Local<v8::Value> sizeVal;
                if (party->Get(context, v8pp::to_v8(isolate, "size")).ToLocal(&sizeVal) && sizeVal->IsArray()) {
                    v8::Local<v8::Array> arr = sizeVal.As<v8::Array>();
                    if (arr->Length() >= 2) {
                        v8::Local<v8::Value> cur;
                        v8::Local<v8::Value> max;
                        if (arr->Get(context, 0).ToLocal(&cur) && arr->Get(context, 1).ToLocal(&max)) {
                            _activity.GetParty().GetSize().SetCurrentSize(cur->Int32Value(context).FromMaybe(0));
                            _activity.GetParty().GetSize().SetMaxSize(max->Int32Value(context).FromMaybe(0));
                        }
                    }
                }
                v8::Local<v8::Value> privacyVal;
                if (party->Get(context, v8pp::to_v8(isolate, "privacy")).ToLocal(&privacyVal) && !privacyVal->IsUndefined()) {
                    _activity.GetParty().SetPrivacy(MapPrivacy(isolate, context, privacyVal));
                }
            }

            // Nested secrets: { match?, join?, spectate? }
            v8::Local<v8::Value> secretsVal;
            if (opts->Get(context, v8pp::to_v8(isolate, "secrets")).ToLocal(&secretsVal) && secretsVal->IsObject()) {
                v8::Local<v8::Object> secrets = secretsVal.As<v8::Object>();
                ReadString(isolate, context, secrets, "match", StringField::MatchSecret);
                ReadString(isolate, context, secrets, "join", StringField::JoinSecret);
                ReadString(isolate, context, secrets, "spectate", StringField::SpectateSecret);
            }
        }

        args.GetReturnValue().Set(Publish());
    }

    void Discord::UpdateCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(Publish());
    }

    void Discord::ClearCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        {
            std::scoped_lock lock(_mutex);
            _activity = discord::Activity {};
        }
        auto *presence = ResolvePresence();
        args.GetReturnValue().Set(presence ? presence->ClearActivity().IsOk() : false);
    }

    void Discord::ResetCallback(const v8::FunctionCallbackInfo<v8::Value> &) {
        std::scoped_lock lock(_mutex);
        _activity = discord::Activity {};
    }

    void Discord::GetUserIdCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        auto *presence = ResolvePresence();
        args.GetReturnValue().Set(v8pp::to_v8(isolate, presence ? presence->GetUserId() : std::string {}));
    }

    void Discord::IsAvailableCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        auto *presence = ResolvePresence();
        args.GetReturnValue().Set(presence && presence->IsInitialized());
    }

    void Discord::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> frameworkObj, Framework::Scripting::ResourceManager *resourceManager) {
        (void)resourceManager;
        if (!isolate || context.IsEmpty() || frameworkObj.IsEmpty()) {
            return;
        }

        v8::Local<v8::Object> discordObj = v8::Object::New(isolate);

        const auto attach = [&](const char *name, v8::FunctionCallback callback, v8::Local<v8::Value> data = v8::Local<v8::Value>()) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback, data);
            discordObj->Set(context, v8pp::to_v8(isolate, name), tmpl->GetFunction(context).ToLocalChecked()).Check();
        };
        const auto strField = [&](const char *name, StringField field) {
            attach(name, &Discord::SetStringFieldCallback, v8::Integer::New(isolate, static_cast<int>(field)));
        };
        const auto numField = [&](const char *name, NumberField field) {
            attach(name, &Discord::SetNumberFieldCallback, v8::Integer::New(isolate, static_cast<int>(field)));
        };

        // Composition
        attach("setType", &Discord::SetTypeCallback);
        strField("setName", StringField::Name);
        strField("setDetails", StringField::Details);
        strField("setState", StringField::State);
        numField("setStartTimestamp", NumberField::StartTimestamp);
        numField("setEndTimestamp", NumberField::EndTimestamp);
        numField("setSupportedPlatforms", NumberField::SupportedPlatforms);
        strField("setLargeImage", StringField::LargeImage);
        strField("setLargeText", StringField::LargeText);
        strField("setSmallImage", StringField::SmallImage);
        strField("setSmallText", StringField::SmallText);
        attach("setAssets", &Discord::SetAssetsCallback);
        strField("setPartyId", StringField::PartyId);
        attach("setPartySize", &Discord::SetPartySizeCallback);
        attach("setPartyPrivacy", &Discord::SetPartyPrivacyCallback);
        attach("setParty", &Discord::SetPartyCallback);
        strField("setMatchSecret", StringField::MatchSecret);
        strField("setJoinSecret", StringField::JoinSecret);
        strField("setSpectateSecret", StringField::SpectateSecret);
        attach("setSecrets", &Discord::SetSecretsCallback);
        attach("setInstance", &Discord::SetInstanceCallback);

        // Commit / lifecycle
        attach("setPresence", &Discord::SetPresenceCallback);
        attach("update", &Discord::UpdateCallback);
        attach("clear", &Discord::ClearCallback);
        attach("reset", &Discord::ResetCallback);

        // Query
        attach("getUserId", &Discord::GetUserIdCallback);
        attach("isAvailable", &Discord::IsAvailableCallback);

        frameworkObj->Set(context, v8pp::to_v8(isolate, "Discord"), discordObj).Check();
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
