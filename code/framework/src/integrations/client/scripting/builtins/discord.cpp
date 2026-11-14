/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "discord.h"

#include <scripting/builtins/script_args.h>

#include "integrations/client/instance.h"

#include "core_modules.h"

#include <external/discord/wrapper.h>
#include <scripting/scripting_catalog.h>

#include <discord.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Framework::Integrations::Client::Scripting::Builtins {

    namespace {
        // Staged activity mutated by the setters, published on update()/setPresence().
        discord::Activity _activity;
        // PresenceField bits staged since the last reset.
        std::uint32_t _written = 0;
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

        std::string ToLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        // One of the two sizes of a party, which JavaScript always converted to a number first: a boolean is
        // 0 or 1, a string the decimal number it spells, between spaces if any. 0 for anything else, a number
        // in another notation included. Read without the locale of the process.
        std::int32_t ToPartySize(const Framework::Scripting::Bind::Value &value) {
            if (value.IsNumber()) {
                return Framework::Scripting::Builtins::ToInt32(value.AsNumber());
            }
            if (value.IsBoolean()) {
                return value.AsBoolean() ? 1 : 0;
            }
            if (!value.IsString()) {
                return 0;
            }
            const std::string text = value.AsString();
            const auto blank       = [](char c) {
                return c == ' ' || (c >= '\t' && c <= '\r');
            };
            const char *first = text.data();
            const char *last  = first + text.size();
            while (first != last && blank(*first)) {
                ++first;
            }
            while (last != first && blank(last[-1])) {
                --last;
            }
            // from_chars takes no plus sign, which JavaScript does, once and before a digit or a point.
            if (last - first > 1 && first[0] == '+' && first[1] != '-') {
                ++first;
            }
            double number     = 0.0;
            const auto parsed = std::from_chars(first, last, number, std::chars_format::general);
            return parsed.ec == std::errc() && parsed.ptr == last ? Framework::Scripting::Builtins::ToInt32(number) : 0;
        }

        discord::ActivityType MapActivityType(const Framework::Scripting::Bind::Value &value) {
            if (value.IsNumber()) {
                return static_cast<discord::ActivityType>(Framework::Scripting::Builtins::ToInt32(value.AsNumber()));
            }
            if (value.IsString()) {
                const std::string name = ToLower(value.AsString());
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

        discord::ActivityPartyPrivacy MapPrivacy(const Framework::Scripting::Bind::Value &value) {
            if (value.IsNumber()) {
                return static_cast<discord::ActivityPartyPrivacy>(Framework::Scripting::Builtins::ToInt32(value.AsNumber()));
            }
            if (value.IsString() && ToLower(value.AsString()) == "public") {
                return discord::ActivityPartyPrivacy::Public;
            }
            return discord::ActivityPartyPrivacy::Private;
        }

        std::uint32_t StringFieldBit(StringField field) {
            switch (field) {
            case StringField::Name: return External::Discord::PresenceField::Name;
            case StringField::Details: return External::Discord::PresenceField::Details;
            case StringField::State: return External::Discord::PresenceField::State;
            case StringField::LargeImage:
            case StringField::LargeText: return External::Discord::PresenceField::LargeImage;
            case StringField::SmallImage:
            case StringField::SmallText: return External::Discord::PresenceField::SmallImage;
            case StringField::PartyId: return External::Discord::PresenceField::Party;
            case StringField::MatchSecret:
            case StringField::JoinSecret:
            case StringField::SpectateSecret: return External::Discord::PresenceField::Secrets;
            }
            return 0;
        }

        // Apply* require the caller to hold _mutex.
        void ApplyString(StringField field, const std::string &value) {
            const char *c = value.c_str();
            _written |= StringFieldBit(field);
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
            _written |= field == NumberField::SupportedPlatforms ? External::Discord::PresenceField::Platforms : External::Discord::PresenceField::Timestamps;
            switch (field) {
            case NumberField::StartTimestamp: _activity.GetTimestamps().SetStart(static_cast<discord::Timestamp>(value)); break;
            case NumberField::EndTimestamp: _activity.GetTimestamps().SetEnd(static_cast<discord::Timestamp>(value)); break;
            case NumberField::SupportedPlatforms: _activity.SetSupportedPlatforms(static_cast<std::uint32_t>(value)); break;
            }
        }

        // What one call stages, read from the script before anything is written: reading a member of an
        // object may run a getter of the script, which may call Discord in turn, and _mutex is not held then.
        class Staging final {
          public:
            void String(StringField field, std::string value) {
                Edit edit;
                edit.kind   = Edit::Kind::String;
                edit.string = field;
                edit.text   = std::move(value);
                _edits.push_back(std::move(edit));
            }

            void Number(NumberField field, std::int64_t value) {
                Edit edit;
                edit.kind    = Edit::Kind::Number;
                edit.number  = field;
                edit.integer = value;
                _edits.push_back(std::move(edit));
            }

            void Type(discord::ActivityType type) {
                Edit edit;
                edit.kind = Edit::Kind::Type;
                edit.type = type;
                _edits.push_back(std::move(edit));
            }

            void PartySize(std::int32_t current, std::int32_t max) {
                Edit edit;
                edit.kind    = Edit::Kind::PartySize;
                edit.current = current;
                edit.max     = max;
                _edits.push_back(std::move(edit));
            }

            void Privacy(discord::ActivityPartyPrivacy privacy) {
                Edit edit;
                edit.kind    = Edit::Kind::Privacy;
                edit.privacy = privacy;
                _edits.push_back(std::move(edit));
            }

            void Instance(bool instance) {
                Edit edit;
                edit.kind = Edit::Kind::Instance;
                edit.flag = instance;
                _edits.push_back(std::move(edit));
            }

            // The member `key` of `options` when it is a string, as `field`.
            void ReadString(const Framework::Scripting::Bind::Value &options, const char *key, StringField field) {
                const Framework::Scripting::Bind::Value value = options.Member(key);
                if (value.IsString()) {
                    String(field, value.AsString());
                }
            }

            void ReadNumber(const Framework::Scripting::Bind::Value &options, const char *key, NumberField field) {
                const Framework::Scripting::Bind::Value value = options.Member(key);
                if (value.IsNumber()) {
                    Number(field, Framework::Scripting::Builtins::ToInt64(value));
                }
            }

            // { id?, size?: [current, max], privacy? }
            void ReadParty(const Framework::Scripting::Bind::Value &party) {
                ReadString(party, "id", StringField::PartyId);

                const Framework::Scripting::Bind::Value size = party.Member("size");
                if (size.IsArray() && size.Length() >= 2) {
                    PartySize(ToPartySize(size.At(0)), ToPartySize(size.At(1)));
                }

                // A privacy that is given and is no name and no number is private, null included.
                const Framework::Scripting::Bind::Value privacy = party.Member("privacy");
                if (!privacy.IsNil() || privacy.IsNull()) {
                    Privacy(MapPrivacy(privacy));
                }
            }

            // { match?, join?, spectate? }
            void ReadSecrets(const Framework::Scripting::Bind::Value &secrets) {
                ReadString(secrets, "match", StringField::MatchSecret);
                ReadString(secrets, "join", StringField::JoinSecret);
                ReadString(secrets, "spectate", StringField::SpectateSecret);
            }

            void ReadAssets(const Framework::Scripting::Bind::Value &assets) {
                ReadString(assets, "largeImage", StringField::LargeImage);
                ReadString(assets, "largeText", StringField::LargeText);
                ReadString(assets, "smallImage", StringField::SmallImage);
                ReadString(assets, "smallText", StringField::SmallText);
            }

            // Writes what was read onto the staged activity, in the order it was read.
            void Commit() const {
                std::scoped_lock lock(_mutex);
                for (const Edit &edit : _edits) {
                    switch (edit.kind) {
                    case Edit::Kind::String: ApplyString(edit.string, edit.text); break;
                    case Edit::Kind::Number: ApplyNumber(edit.number, edit.integer); break;
                    case Edit::Kind::Type:
                        _activity.SetType(edit.type);
                        _written |= External::Discord::PresenceField::Type;
                        break;
                    case Edit::Kind::PartySize:
                        _activity.GetParty().GetSize().SetCurrentSize(edit.current);
                        _activity.GetParty().GetSize().SetMaxSize(edit.max);
                        _written |= External::Discord::PresenceField::Party;
                        break;
                    case Edit::Kind::Privacy:
                        _activity.GetParty().SetPrivacy(edit.privacy);
                        _written |= External::Discord::PresenceField::Party;
                        break;
                    case Edit::Kind::Instance:
                        _activity.SetInstance(edit.flag);
                        _written |= External::Discord::PresenceField::Instance;
                        break;
                    }
                }
            }

          private:
            struct Edit final {
                enum class Kind {
                    String,
                    Number,
                    Type,
                    PartySize,
                    Privacy,
                    Instance,
                };

                Kind kind          = Kind::String;
                StringField string = StringField::Name;
                NumberField number = NumberField::StartTimestamp;
                std::string text;
                std::int64_t integer                  = 0;
                discord::ActivityType type            = discord::ActivityType::Playing;
                discord::ActivityPartyPrivacy privacy = discord::ActivityPartyPrivacy::Private;
                std::int32_t current                  = 0;
                std::int32_t max                      = 0;
                bool flag                             = false;
            };

            std::vector<Edit> _edits;
        };

        // What JavaScript calls an object: a plain object or a table, an array, a function, an instance of a
        // bound class. Members are read off the first two and off a bound instance; a function has none here.
        bool IsObject(const Framework::Scripting::Bind::Value &value) {
            switch (value.Kind()) {
            case Framework::Scripting::Bind::ValueKind::Object:
            case Framework::Scripting::Bind::ValueKind::Array:
            case Framework::Scripting::Bind::ValueKind::Function:
            case Framework::Scripting::Bind::ValueKind::Bound: return true;
            default: return false;
            }
        }

        bool Publish() {
            auto *presence = ResolvePresence();
            if (!presence || !presence->IsInitialized()) {
                return false;
            }
            discord::Activity snapshot {};
            std::uint32_t written = 0;
            {
                std::scoped_lock lock(_mutex);
                snapshot = _activity;
                written  = _written;
            }
            presence->SetScriptActivity(snapshot, written);
            return true;
        }

        void ResetStaged() {
            std::scoped_lock lock(_mutex);
            _activity = discord::Activity {};
            _written  = 0;
        }

        void SetStringField(Framework::Scripting::Bind::Call &call, StringField field) {
            if (!call.Arg(0).IsString()) {
                call.ThrowError("Discord: expected a string argument");
                return;
            }
            const std::string value = call.Arg(0).AsString();
            std::scoped_lock lock(_mutex);
            ApplyString(field, value);
        }

        void SetNumberField(Framework::Scripting::Bind::Call &call, NumberField field) {
            if (!call.Arg(0).IsNumber()) {
                call.ThrowError("Discord: expected a numeric argument");
                return;
            }
            const std::int64_t value = Framework::Scripting::Builtins::ToInt64(call.Arg(0));
            std::scoped_lock lock(_mutex);
            ApplyNumber(field, value);
        }

        Framework::Scripting::Bind::NativeFunction StringSetter(StringField field) {
            return [field](Framework::Scripting::Bind::Call &call) {
                SetStringField(call, field);
            };
        }

        Framework::Scripting::Bind::NativeFunction NumberSetter(NumberField field) {
            return [field](Framework::Scripting::Bind::Call &call) {
                SetNumberField(call, field);
            };
        }

        void SetType(Framework::Scripting::Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowError("Discord.setType: expected (type)");
                return;
            }
            Staging staging;
            staging.Type(MapActivityType(call.Arg(0)));
            staging.Commit();
        }

        void SetPartySize(Framework::Scripting::Bind::Call &call) {
            if (!call.Arg(0).IsNumber() || !call.Arg(1).IsNumber()) {
                call.ThrowError("Discord.setPartySize: expected (current, max)");
                return;
            }
            Staging staging;
            staging.PartySize(Framework::Scripting::Builtins::ToInt32(call.Arg(0).AsNumber()), Framework::Scripting::Builtins::ToInt32(call.Arg(1).AsNumber()));
            staging.Commit();
        }

        void SetPartyPrivacy(Framework::Scripting::Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowError("Discord.setPartyPrivacy: expected (privacy)");
                return;
            }
            Staging staging;
            staging.Privacy(MapPrivacy(call.Arg(0)));
            staging.Commit();
        }

        void SetInstance(Framework::Scripting::Bind::Call &call) {
            if (call.ArgCount() < 1) {
                call.ThrowError("Discord.setInstance: expected (bool)");
                return;
            }
            Staging staging;
            staging.Instance(Framework::Scripting::Builtins::ReadFlag(call, 0));
            staging.Commit();
        }

        void SetAssets(Framework::Scripting::Bind::Call &call) {
            if (!IsObject(call.Arg(0))) {
                call.ThrowError("Discord.setAssets: expected an object");
                return;
            }
            Staging staging;
            staging.ReadAssets(call.Arg(0));
            staging.Commit();
        }

        void SetParty(Framework::Scripting::Bind::Call &call) {
            if (!IsObject(call.Arg(0))) {
                call.ThrowError("Discord.setParty: expected an object");
                return;
            }
            Staging staging;
            staging.ReadParty(call.Arg(0));
            staging.Commit();
        }

        void SetSecrets(Framework::Scripting::Bind::Call &call) {
            if (!IsObject(call.Arg(0))) {
                call.ThrowError("Discord.setSecrets: expected an object");
                return;
            }
            Staging staging;
            staging.ReadSecrets(call.Arg(0));
            staging.Commit();
        }

        void SetPresence(Framework::Scripting::Bind::Call &call) {
            const Framework::Scripting::Bind::Value options = call.Arg(0);
            if (!IsObject(options)) {
                call.ThrowError("Discord.setPresence: expected an options object");
                return;
            }

            Staging staging;
            // A type that is given and is no name and no number is "playing", null included.
            const Framework::Scripting::Bind::Value type = options.Member("type");
            if (!type.IsNil() || type.IsNull()) {
                staging.Type(MapActivityType(type));
            }

            staging.ReadString(options, "name", StringField::Name);
            staging.ReadString(options, "details", StringField::Details);
            staging.ReadString(options, "state", StringField::State);

            staging.ReadNumber(options, "startTimestamp", NumberField::StartTimestamp);
            staging.ReadNumber(options, "endTimestamp", NumberField::EndTimestamp);
            staging.ReadNumber(options, "supportedPlatforms", NumberField::SupportedPlatforms);

            staging.ReadAssets(options);

            const Framework::Scripting::Bind::Value instance = options.Member("instance");
            if (instance.IsBoolean()) {
                staging.Instance(instance.AsBoolean());
            }

            const Framework::Scripting::Bind::Value party = options.Member("party");
            if (IsObject(party)) {
                staging.ReadParty(party);
            }

            const Framework::Scripting::Bind::Value secrets = options.Member("secrets");
            if (IsObject(secrets)) {
                staging.ReadSecrets(secrets);
            }

            staging.Commit();
            call.Return(Publish());
        }

        void Update(Framework::Scripting::Bind::Call &call) {
            call.Return(Publish());
        }

        void Clear(Framework::Scripting::Bind::Call &call) {
            ResetStaged();
            auto *presence = ResolvePresence();
            if (!presence || !presence->IsInitialized()) {
                call.Return(false);
                return;
            }
            presence->ClearScriptActivity();
            call.Return(true);
        }

        void Reset(Framework::Scripting::Bind::Call &) {
            ResetStaged();
        }

        void GetUserId(Framework::Scripting::Bind::Call &call) {
            auto *presence = ResolvePresence();
            call.Return(presence ? presence->GetUserId() : std::string {});
        }

        void GetUsername(Framework::Scripting::Bind::Call &call) {
            auto *presence = ResolvePresence();
            call.Return(presence ? presence->GetUsername() : std::string {});
        }

        void IsAvailable(Framework::Scripting::Bind::Call &call) {
            auto *presence = ResolvePresence();
            call.Return(presence && presence->IsInitialized());
        }
    } // anonymous namespace

    void Discord::Shutdown() {
        ResetStaged();
        if (auto *presence = ResolvePresence()) {
            presence->ClearScriptActivity();
        }
    }

    const Framework::Scripting::Bind::Namespace &Discord::Definition() {
        static const auto definition = [] {
            const auto number  = Framework::Scripting::Bind::TypeOf<double>();
            const auto boolean = Framework::Scripting::Bind::TypeOf<bool>();
            const auto text    = Framework::Scripting::Bind::TypeOf<std::string>();
            const auto type    = Framework::Scripting::Bind::Type::Custom("\"playing\" | \"streaming\" | \"listening\" | \"watching\" | number", "'playing'|'streaming'|'listening'|'watching'|integer");
            const auto privacy = Framework::Scripting::Bind::Type::Custom("\"private\" | \"public\" | number", "'private'|'public'|integer");
            const auto assets  = Framework::Scripting::Bind::Type::Custom("{ largeImage?: string; largeText?: string; smallImage?: string; smallText?: string }", "{ largeImage: string?, largeText: string?, smallImage: string?, smallText: string? }");
            const auto party   = Framework::Scripting::Bind::Type::Custom("{ id?: string; size?: [number, number]; privacy?: \"private\" | \"public\" | number }", "{ id: string?, size: integer[]?, privacy: ('private'|'public'|integer)? }");
            const auto secrets = Framework::Scripting::Bind::Type::Custom("{ match?: string; join?: string; spectate?: string }", "{ match: string?, join: string?, spectate: string? }");
            const auto options = Framework::Scripting::Bind::Type::Custom("Record<string, unknown>", "table<string, any>");

            // A setter of one field: its summary is what its parameter is, staged.
            const auto field = [](const Framework::Scripting::Bind::TypeRef &of, const char *parameter, const char *description) {
                return Framework::Scripting::Bind::Doc(std::string("Stages ") + description).Param(parameter, of, description);
            };

            // Composition, then commit and lifecycle, then queries.
            return Framework::Scripting::Bind::Namespace("Discord", "Client-only Discord rich-presence composer exposed as the global Discord; setters stage values until update or setPresence publishes them.", Framework::Scripting::Bind::Side::Client)
                .Function("setType", &SetType, field(type, "type", "the Discord activity verb or numeric enum value."), Framework::Scripting::Bind::Side::Client)
                .Function("setName", StringSetter(StringField::Name), field(text, "name", "the activity name."), Framework::Scripting::Bind::Side::Client)
                .Function("setDetails", StringSetter(StringField::Details), field(text, "details", "the top presence line."), Framework::Scripting::Bind::Side::Client)
                .Function("setState", StringSetter(StringField::State), field(text, "state", "the bottom presence line."), Framework::Scripting::Bind::Side::Client)
                .Function("setStartTimestamp", NumberSetter(NumberField::StartTimestamp), field(number, "seconds", "the elapsed-time Unix timestamp in seconds; zero clears it."), Framework::Scripting::Bind::Side::Client)
                .Function("setEndTimestamp", NumberSetter(NumberField::EndTimestamp), field(number, "seconds", "the countdown Unix timestamp in seconds; zero clears it."), Framework::Scripting::Bind::Side::Client)
                .Function("setSupportedPlatforms", NumberSetter(NumberField::SupportedPlatforms), field(number, "flags", "the supported-platform bitmask: Desktop 1, Android 2, and iOS 4."), Framework::Scripting::Bind::Side::Client)
                .Function("setLargeImage", StringSetter(StringField::LargeImage), field(text, "assetKey", "the large image asset key."), Framework::Scripting::Bind::Side::Client)
                .Function("setLargeText", StringSetter(StringField::LargeText), field(text, "text", "the large image tooltip."), Framework::Scripting::Bind::Side::Client)
                .Function("setSmallImage", StringSetter(StringField::SmallImage), field(text, "assetKey", "the small image asset key."), Framework::Scripting::Bind::Side::Client)
                .Function("setSmallText", StringSetter(StringField::SmallText), field(text, "text", "the small image tooltip."), Framework::Scripting::Bind::Side::Client)
                .Function("setAssets", &SetAssets, Framework::Scripting::Bind::Doc("Stages multiple Discord image fields at once.").Param("assets", assets, "Asset keys and tooltips to merge into the staged activity."), Framework::Scripting::Bind::Side::Client)
                .Function("setPartyId", StringSetter(StringField::PartyId), field(text, "id", "the party grouping identifier."), Framework::Scripting::Bind::Side::Client)
                .Function("setPartySize", &SetPartySize, Framework::Scripting::Bind::Doc("Stages the party's current and maximum size.").Param("current", number, "Current party member count.").Param("max", number, "Maximum party capacity."), Framework::Scripting::Bind::Side::Client)
                .Function("setPartyPrivacy", &SetPartyPrivacy, field(privacy, "privacy", "the party join-privacy name or numeric enum value."), Framework::Scripting::Bind::Side::Client)
                .Function("setParty", &SetParty, Framework::Scripting::Bind::Doc("Stages multiple Discord party fields at once.").Param("party", party, "Party fields to merge into the staged activity."), Framework::Scripting::Bind::Side::Client)
                .Function("setMatchSecret", StringSetter(StringField::MatchSecret), field(text, "secret", "the opaque match secret."), Framework::Scripting::Bind::Side::Client)
                .Function("setJoinSecret", StringSetter(StringField::JoinSecret), field(text, "secret", "the opaque join secret that enables invitations."), Framework::Scripting::Bind::Side::Client)
                .Function("setSpectateSecret", StringSetter(StringField::SpectateSecret), field(text, "secret", "the opaque spectate secret."), Framework::Scripting::Bind::Side::Client)
                .Function("setSecrets", &SetSecrets, Framework::Scripting::Bind::Doc("Stages multiple Discord activity secrets at once.").Param("secrets", secrets, "Opaque secrets to merge into the staged activity."), Framework::Scripting::Bind::Side::Client)
                .Function("setInstance", &SetInstance, field(boolean, "instance", "whether the activity represents an instanced session."), Framework::Scripting::Bind::Side::Client)
                .Function("setPresence", &SetPresence,
                    Framework::Scripting::Bind::Doc("Merges a complete option batch into the staged activity and publishes it immediately.")
                        .Param("options", options, "Batch of supported activity, timestamp, asset, party, secret, instance, and platform fields.")
                        .ReturnType(boolean)
                        .Returns("True when the update was dispatched; false when Discord is unavailable."),
                    Framework::Scripting::Bind::Side::Client)
                .Function("update", &Update, Framework::Scripting::Bind::Doc("Publishes the currently staged activity as one rate-limited Discord update.").ReturnType(boolean).Returns("True when dispatched; false when Discord is unavailable."), Framework::Scripting::Bind::Side::Client)
                .Function("clear", &Clear, Framework::Scripting::Bind::Doc("Clears what scripts published and resets staged state.").ReturnType(boolean).Returns("True when dispatched; false when Discord is unavailable."), Framework::Scripting::Bind::Side::Client)
                .Function("reset", &Reset, Framework::Scripting::Bind::Doc("Resets staged activity fields without publishing a Discord update."), Framework::Scripting::Bind::Side::Client)
                .Function("getUserId", &GetUserId, Framework::Scripting::Bind::Doc("Returns the signed-in Discord user's snowflake.").ReturnType(text).Returns("User ID string, or an empty string until available."), Framework::Scripting::Bind::Side::Client)
                .Function("getUsername", &GetUsername, Framework::Scripting::Bind::Doc("Returns the signed-in Discord user's username.").ReturnType(text).Returns("Username string, or an empty string until available."), Framework::Scripting::Bind::Side::Client)
                .Function("isAvailable", &IsAvailable, Framework::Scripting::Bind::Doc("Checks whether Discord is connected and can publish presence.").ReturnType(boolean).Returns("True when rich presence is initialized."), Framework::Scripting::Bind::Side::Client);
        }();
        return definition;
    }

    void Discord::Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages, Framework::Scripting::ResourceManager *resourceManager) {
        if (registrar.HasJavaScript(languages) && resourceManager != nullptr) {
            Framework::Scripting::Bind::SetCallResourceManager(registrar.Isolate(), resourceManager);
        }
        registrar.Global(Definition(), languages);
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
