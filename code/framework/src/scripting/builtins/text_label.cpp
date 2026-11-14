/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "text_label.h"
#include "../resource/resource_manager.h"
#include "../scripting_catalog.h"
#include "color.h"
#include "vector3.h"

#include <core_modules.h>
#include <external/imgui/widgets/world_text_markup.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace Framework::Scripting::Builtins {
    namespace {
        // Background padding and corner radius, in pixels.
        constexpr double kMaxBlockPixels = 64.0;

        // Everything an options object can carry, validated in full before any of it is applied, so
        // a bad key leaves the label as it was.
        struct Options {
            struct Outline {
                float width    = 1.0f;
                uint32_t color = 0xFF000000;
            };
            struct Shadow {
                uint32_t color = 0xFF000000;
                float offsetX  = 1.0f;
                float offsetY  = 1.0f;
                float blur     = 0.0f;
            };
            struct Background {
                uint32_t color = 0x99000000;
                float padding  = 4.0f;
                float radius   = 4.0f;
            };

            std::optional<std::string> text;
            std::optional<bool> markup;
            std::optional<glm::vec3> position;
            std::optional<uint64_t> attachTo; // 0 detaches
            std::optional<glm::vec3> offset;
            std::optional<float> fontSize;
            std::optional<Networking::Replication::Entities::TextLabelSizeMode> sizeMode;
            std::optional<float> minSize;
            std::optional<float> maxSize;
            std::optional<uint32_t> color;
            std::optional<float> opacity;
            std::optional<Networking::Replication::Entities::TextLabelStyle> style;
            std::optional<std::optional<Outline>> outline; // set but empty = turned off
            std::optional<std::optional<Shadow>> shadow;
            std::optional<std::optional<Background>> background;
            std::optional<Networking::Replication::Entities::TextLabelAlign> align;
            std::optional<Networking::Replication::Entities::TextLabelAnchor> anchor;
            std::optional<float> lineSpacing;
            std::optional<float> maxWidth;
            std::optional<float> drawDistance;
            std::optional<float> fadeDistance;
            std::optional<bool> occlusion;
            std::optional<uint32_t> virtualWorld;
            std::optional<std::optional<std::vector<uint64_t>>> showTo; // set to nullopt = everyone, to {} = nobody
            std::optional<std::vector<uint64_t>> hideFrom;
            std::optional<bool> hideForTarget;

            // showTo / hideFrom as connections, filled by CheckViewers once replication is known.
            std::vector<MafiaNet::PeerGuid> showToGuids;
            std::vector<MafiaNet::PeerGuid> hideFromGuids;
        };

        constexpr std::string_view kOptionKeys[] {"text", "markup", "position", "attachTo", "offset", "fontSize", "sizeMode", "minSize", "maxSize", "color", "opacity", "style", "outline", "shadow", "background", "align", "anchor", "lineSpacing", "maxWidth", "drawDistance",
            "fadeDistance", "occlusion", "virtualWorld", "showTo", "hideFrom", "hideForTarget"};

        constexpr std::pair<std::string_view, Networking::Replication::Entities::TextLabelStyle> kStyles[] {{"none", Networking::Replication::Entities::TextLabelStyle::None}, {"shadow", Networking::Replication::Entities::TextLabelStyle::Shadow},
            {"outline", Networking::Replication::Entities::TextLabelStyle::Outline}, {"box", Networking::Replication::Entities::TextLabelStyle::Box}};
        constexpr std::pair<std::string_view, Networking::Replication::Entities::TextLabelSizeMode> kSizeModes[] {{"screen", Networking::Replication::Entities::TextLabelSizeMode::Screen}, {"world", Networking::Replication::Entities::TextLabelSizeMode::World}};
        constexpr std::pair<std::string_view, Networking::Replication::Entities::TextLabelAlign> kAligns[] {{"left", Networking::Replication::Entities::TextLabelAlign::Left}, {"center", Networking::Replication::Entities::TextLabelAlign::Center},
            {"right", Networking::Replication::Entities::TextLabelAlign::Right}};
        constexpr std::pair<std::string_view, Networking::Replication::Entities::TextLabelAnchor> kAnchors[] {{"center", Networking::Replication::Entities::TextLabelAnchor::Center}, {"bottom", Networking::Replication::Entities::TextLabelAnchor::Bottom},
            {"top", Networking::Replication::Entities::TextLabelAnchor::Top}};

        bool Fail(Bind::Call &call, const std::string &message) {
            call.ThrowTypeError("TextLabel: " + message);
            return false;
        }

        // A plain Error, for an argument of the right type that names a player no longer connected.
        bool FailError(Bind::Call &call, const std::string &message) {
            call.ThrowError("TextLabel: " + message);
            return false;
        }

        // What stands for "none" where an option takes null with a meaning: null in JavaScript. A Lua table
        // cannot hold nil, so a Lua script passes false.
        bool IsNone(const Bind::Call &call, const Bind::Value &value) {
            if (call.GetLanguage() == Language::Lua) {
                return value.IsBoolean() && !value.AsBoolean();
            }
            return value.IsNull();
        }

        // What JavaScript calls an object and not an array: a plain object or table, a bound object, a function.
        // Only the first has keys to read (bind/value.h).
        bool IsObject(const Bind::Value &value) {
            const Bind::ValueKind kind = value.Kind();
            return kind == Bind::ValueKind::Object || kind == Bind::ValueKind::Bound || kind == Bind::ValueKind::Function;
        }

        std::optional<float> Number(Bind::Call &call, const Bind::Value &value, const std::string &name, double min, double max) {
            const double number = value.IsNumber() ? value.AsNumber() : NAN;
            if (!std::isfinite(number) || number < min || number > max) {
                std::ostringstream ss;
                ss << name << " must be a number from " << min << " to " << max;
                Fail(call, ss.str());
                return std::nullopt;
            }
            return static_cast<float>(number);
        }

        std::optional<bool> Bool(Bind::Call &call, const Bind::Value &value, const std::string &name) {
            if (!value.IsBoolean()) {
                Fail(call, name + " must be a boolean");
                return std::nullopt;
            }
            return value.AsBoolean();
        }

        // 0xAARRGGBB (or its signed int32 form), "#RRGGBB" / "#RRGGBBAA", or a Color.
        std::optional<uint32_t> ColorValue(Bind::Call &call, const Bind::Value &value, const std::string &name) {
            if (value.IsNumber()) {
                const double n = value.AsNumber();
                if (std::isfinite(n) && std::floor(n) == n && n >= -2147483648.0 && n <= 4294967295.0) {
                    return n < 0 ? static_cast<uint32_t>(static_cast<int32_t>(n)) : static_cast<uint32_t>(n);
                }
            }
            else if (value.IsString()) {
                if (const auto parsed = External::ImGUI::Widgets::Detail::ParseHexColor(value.AsString())) {
                    return parsed;
                }
            }
            else if (const auto *color = value.AsBound<Color>()) {
                return color->toARGB();
            }
            Fail(call, name + " must be 0xAARRGGBB, \"#RRGGBB[AA]\" or a Color");
            return std::nullopt;
        }

        // A Vector3, {x, y, z} or [x, y, z].
        std::optional<glm::vec3> Vec3(Bind::Call &call, const Bind::Value &value, const std::string &name) {
            if (const auto *vector = value.AsBound<Vector3>()) {
                return glm::vec3(vector->getX(), vector->getY(), vector->getZ());
            }
            Bind::Value parts[3];
            const auto kind = value.Kind();
            if (kind == Bind::ValueKind::Array && value.Length() == 3) {
                for (size_t i = 0; i < 3; ++i) {
                    parts[i] = value.At(i);
                    if (call.HasThrown()) {
                        return std::nullopt;
                    }
                }
            }
            else if (kind == Bind::ValueKind::Array || IsObject(value)) {
                constexpr std::string_view keys[] {"x", "y", "z"};
                for (int i = 0; i < 3; ++i) {
                    // As a script reads them: the keys of a table, the properties of any bound vector. Empty when
                    // a getter threw.
                    parts[i] = value.Member(keys[i]);
                    if (call.HasThrown()) {
                        return std::nullopt;
                    }
                }
            }
            else {
                Fail(call, name + " must be a Vector3, {x, y, z} or [x, y, z]");
                return std::nullopt;
            }
            glm::vec3 out(0.0f);
            for (int i = 0; i < 3; ++i) {
                const auto component = Number(call, parts[i], name, -1.0e9, 1.0e9);
                if (!component) {
                    return std::nullopt;
                }
                out[i] = *component;
            }
            return out;
        }

        template <typename T, size_t N>
        std::optional<T> Enum(Bind::Call &call, const Bind::Value &value, const std::string &name, const std::pair<std::string_view, T> (&entries)[N]) {
            if (value.IsString()) {
                const std::string text = value.AsString();
                for (const auto &[label, entry] : entries) {
                    if (text == label) {
                        return entry;
                    }
                }
            }
            std::string accepted;
            for (const auto &[label, entry] : entries) {
                accepted += (accepted.empty() ? "'" : ", '") + std::string(label) + "'";
            }
            Fail(call, name + " must be one of " + accepted);
            return std::nullopt;
        }

        // An entity handle (anything with a numeric id) or a network id. `none` is whether the value
        // stands for no entity, which detaches, as 0 does.
        std::optional<uint64_t> Target(Bind::Call &call, const Bind::Value &value, const std::string &name, bool none) {
            if (none) {
                return 0;
            }
            // The id of a handle is read as a script reads it, so a handle of any bound class has one, whether or
            // not it derives from Entity.
            Bind::Value id  = value;
            const auto kind = value.Kind();
            if (kind == Bind::ValueKind::Object || kind == Bind::ValueKind::Array || kind == Bind::ValueKind::Bound) {
                id = value.Member("id");
                if (call.HasThrown()) {
                    return std::nullopt;
                }
            }
            const double n = id.IsNumber() ? id.AsNumber() : NAN;
            if (!std::isfinite(n) || n < 0.0 || n > 9007199254740991.0 || std::floor(n) != n) {
                Fail(call, name + " must be an entity or its network id");
                return std::nullopt;
            }
            return static_cast<uint64_t>(n);
        }

        // A player handle or network id. Unlike for attachTo, nothing is not one.
        std::optional<uint64_t> PlayerId(Bind::Call &call, const Bind::Value &value, const std::string &name) {
            if (value.IsNil()) {
                Fail(call, name + " must be a Player or their network id");
                return std::nullopt;
            }
            return Target(call, value, name, false);
        }

        // An array of players, as handles or network ids. The empty table of Lua is an empty list; the empty
        // object of JavaScript is not an array.
        std::optional<std::vector<uint64_t>> PlayerIds(Bind::Call &call, const Bind::Value &value, const std::string &name) {
            const bool lua = call.GetLanguage() == Language::Lua;
            if (!(lua ? value.IsList() : value.IsArray())) {
                Fail(call, name + " must be an array of Players or their network ids");
                return std::nullopt;
            }
            // Read once: an element's getter can grow the array.
            const size_t length = value.Length();
            std::vector<uint64_t> ids;
            for (size_t i = 0; i < length; ++i) {
                const Bind::Value entry = value.At(i);
                if (call.HasThrown()) {
                    return std::nullopt;
                }
                // Named as the script counts it.
                const auto id = PlayerId(call, entry, name + "[" + std::to_string(lua ? i + 1 : i) + "]");
                if (!id) {
                    return std::nullopt;
                }
                ids.push_back(*id);
            }
            return ids;
        }

        // false/null turns a block off, true takes its defaults, an object sets it whole.
        enum class BlockState {
            Invalid,
            Off,
            Defaults,
            Object,
        };
        BlockState Block(Bind::Call &call, const Bind::Value &value, const std::string &name) {
            if (value.IsNull() || (value.IsBoolean() && !value.AsBoolean())) {
                return BlockState::Off;
            }
            if (value.IsBoolean()) {
                return BlockState::Defaults;
            }
            if (IsObject(value)) {
                return BlockState::Object;
            }
            Fail(call, name + " must be an object, true or false");
            return BlockState::Invalid;
        }

        // Reads `key` off a block when present; absent keeps `out` at its default.
        template <typename T, typename Reader>
        bool BlockField(Bind::Call &call, const Bind::Value &block, std::string_view key, T &out, const Reader &read) {
            const Bind::Value value = block.Get(key);
            if (call.HasThrown()) {
                return false;
            }
            if (value.IsNil() && !value.IsNull()) {
                return true;
            }
            const auto parsed = read(value);
            if (!parsed) {
                return false;
            }
            out = *parsed;
            return true;
        }

        bool ParseOptions(Bind::Call &call, const Bind::Value &object, float unitsPerMetre, Options &options) {
            for (const std::string &key : object.Keys()) {
                if (std::find(std::begin(kOptionKeys), std::end(kOptionKeys), key) == std::end(kOptionKeys)) {
                    return Fail(call, "unknown option '" + key + "'");
                }
            }
            // The keys could not be listed: the trap of a proxy threw.
            if (call.HasThrown()) {
                return false;
            }

            const double maxDistance = TextLabel::kMaxDrawMetres * unitsPerMetre;
            for (const std::string_view key : kOptionKeys) {
                const Bind::Value value = object.Get(key);
                if (call.HasThrown()) {
                    return false;
                }
                // A key left out. null is a value, wrong for most keys and with a meaning of its own for some.
                if (value.IsNil() && !value.IsNull()) {
                    continue;
                }
                const std::string name(key);
                const auto number = [&](double min, double max, std::optional<float> &out) {
                    out = Number(call, value, name, min, max);
                    return out.has_value();
                };

                bool ok = true;
                if (key == "text") {
                    ok = value.IsString() || Fail(call, "text must be a string");
                    if (ok) {
                        options.text = value.AsString();
                    }
                }
                else if (key == "markup") {
                    ok = (options.markup = Bool(call, value, name)).has_value();
                }
                else if (key == "position") {
                    ok = (options.position = Vec3(call, value, name)).has_value();
                }
                else if (key == "attachTo") {
                    ok = (options.attachTo = Target(call, value, name, IsNone(call, value))).has_value();
                }
                else if (key == "offset") {
                    ok = (options.offset = Vec3(call, value, name)).has_value();
                }
                else if (key == "fontSize") {
                    ok = number(TextLabel::kMinFontSize, TextLabel::kMaxFontSize, options.fontSize);
                }
                else if (key == "sizeMode") {
                    ok = (options.sizeMode = Enum(call, value, name, kSizeModes)).has_value();
                }
                else if (key == "minSize") {
                    ok = number(0.0, TextLabel::kMaxFontSize, options.minSize);
                }
                else if (key == "maxSize") {
                    ok = number(0.0, TextLabel::kMaxFontSize, options.maxSize);
                }
                else if (key == "color") {
                    ok = (options.color = ColorValue(call, value, name)).has_value();
                }
                else if (key == "opacity") {
                    ok = number(0.0, 1.0, options.opacity);
                }
                else if (key == "style") {
                    if (value.IsNumber()) {
                        const double raw = value.AsNumber();
                        ok               = std::isfinite(raw) && raw >= 0.0 && raw <= static_cast<double>(Networking::Replication::Entities::TextLabelStyle::Box) && std::floor(raw) == raw;
                        if (ok) {
                            options.style = static_cast<Networking::Replication::Entities::TextLabelStyle>(static_cast<uint8_t>(raw));
                        }
                        else {
                            Fail(call, "style must be 'none', 'shadow', 'outline', 'box' or 0-3");
                        }
                    }
                    else {
                        ok = (options.style = Enum(call, value, name, kStyles)).has_value();
                    }
                }
                else if (key == "outline") {
                    const auto state = Block(call, value, name);
                    ok               = state != BlockState::Invalid;
                    Options::Outline outline;
                    if (state == BlockState::Object) {
                        ok = BlockField(call, value, "color", outline.color, [&](const Bind::Value &v) {
                            return ColorValue(call, v, "outline.color");
                        }) && BlockField(call, value, "width", outline.width, [&](const Bind::Value &v) {
                            return Number(call, v, "outline.width", 0.0, Networking::Replication::Entities::kTextLabelMaxOutlineWidth);
                        });
                    }
                    if (ok) {
                        options.outline = state == BlockState::Off ? std::optional<Options::Outline> {} : std::optional<Options::Outline> {outline};
                    }
                }
                else if (key == "shadow") {
                    const auto state = Block(call, value, name);
                    ok               = state != BlockState::Invalid;
                    Options::Shadow shadow;
                    if (state == BlockState::Object) {
                        ok = BlockField(call, value, "color", shadow.color, [&](const Bind::Value &v) {
                            return ColorValue(call, v, "shadow.color");
                        }) && BlockField(call, value, "offsetX", shadow.offsetX, [&](const Bind::Value &v) {
                            return Number(call, v, "shadow.offsetX", -32.0, 32.0);
                        }) && BlockField(call, value, "offsetY", shadow.offsetY, [&](const Bind::Value &v) {
                            return Number(call, v, "shadow.offsetY", -32.0, 32.0);
                        }) && BlockField(call, value, "blur", shadow.blur, [&](const Bind::Value &v) {
                            return Number(call, v, "shadow.blur", 0.0, Networking::Replication::Entities::kTextLabelMaxShadowBlur);
                        });
                    }
                    if (ok) {
                        options.shadow = state == BlockState::Off ? std::optional<Options::Shadow> {} : std::optional<Options::Shadow> {shadow};
                    }
                }
                else if (key == "background") {
                    const auto state = Block(call, value, name);
                    ok               = state != BlockState::Invalid;
                    Options::Background background;
                    if (state == BlockState::Object) {
                        ok = BlockField(call, value, "color", background.color, [&](const Bind::Value &v) {
                            return ColorValue(call, v, "background.color");
                        }) && BlockField(call, value, "padding", background.padding, [&](const Bind::Value &v) {
                            return Number(call, v, "background.padding", 0.0, kMaxBlockPixels);
                        }) && BlockField(call, value, "radius", background.radius, [&](const Bind::Value &v) {
                            return Number(call, v, "background.radius", 0.0, kMaxBlockPixels);
                        });
                    }
                    if (ok) {
                        options.background = state == BlockState::Off ? std::optional<Options::Background> {} : std::optional<Options::Background> {background};
                    }
                }
                else if (key == "align") {
                    ok = (options.align = Enum(call, value, name, kAligns)).has_value();
                }
                else if (key == "anchor") {
                    ok = (options.anchor = Enum(call, value, name, kAnchors)).has_value();
                }
                else if (key == "lineSpacing") {
                    ok = number(0.5, 4.0, options.lineSpacing);
                }
                else if (key == "maxWidth") {
                    ok = number(0.0, 4096.0, options.maxWidth);
                }
                else if (key == "drawDistance") {
                    ok = number(1.0, maxDistance, options.drawDistance);
                }
                else if (key == "fadeDistance") {
                    ok = number(0.0, maxDistance, options.fadeDistance);
                }
                else if (key == "occlusion") {
                    ok = (options.occlusion = Bool(call, value, name)).has_value();
                }
                else if (key == "virtualWorld") {
                    const double world = value.IsNumber() ? value.AsNumber() : NAN;
                    ok                 = std::isfinite(world) && world >= 0.0 && world <= 4294967295.0 && std::floor(world) == world;
                    if (ok) {
                        options.virtualWorld = static_cast<uint32_t>(world);
                    }
                    else {
                        Fail(call, "virtualWorld must be a whole number from 0 to 4294967295");
                    }
                }
                else if (key == "showTo") {
                    if (IsNone(call, value)) {
                        options.showTo = std::optional<std::vector<uint64_t>> {};
                    }
                    else if (auto ids = PlayerIds(call, value, name)) {
                        options.showTo.emplace(std::move(*ids));
                    }
                    else {
                        ok = false;
                    }
                }
                else if (key == "hideFrom") {
                    ok = (options.hideFrom = IsNone(call, value) ? std::vector<uint64_t> {} : PlayerIds(call, value, name)).has_value();
                }
                else if (key == "hideForTarget") {
                    ok = (options.hideForTarget = Bool(call, value, name)).has_value();
                }
                if (!ok) {
                    return false;
                }
            }
            return true;
        }

        // Against the label's current limits, or a new label's when there is none yet.
        bool CheckSizes(Bind::Call &call, const Options &options, const Networking::Replication::Entities::TextLabelEntity *label) {
            const float minSize = options.minSize.value_or(label ? label->minSize : 0.0f);
            const float maxSize = options.maxSize.value_or(label ? label->maxSize : 0.0f);
            return maxSize <= 0.0f || minSize <= maxSize || Fail(call, "minSize must not exceed maxSize");
        }

        // `handle` is null while the label is still to be created, so it cannot be its own target.
        bool CheckTarget(Bind::Call &call, const Options &options, const TextLabel *handle) {
            if (!options.attachTo || *options.attachTo == 0) {
                return true;
            }
            auto *replication     = CoreModules::GetReplication();
            const bool attachable = handle ? handle->CanAttachTo(*options.attachTo) : replication && replication->GetEntityByNetworkID(*options.attachTo) != nullptr;
            return attachable || Fail(call, "attachTo must be another live entity");
        }

        // A player's connection, found from their avatar: what a viewer filter is keyed on. An id that
        // is no player's current avatar (gone, an NPC, or an entity they merely own) is an Error, so a
        // player can only be named once SetViewer has run for them.
        bool ResolvePlayer(Bind::Call &call, const Networking::Replication::ReplicationManager &replication, uint64_t id, const std::string &name, MafiaNet::PeerGuid &guid) {
            const auto *entity = replication.GetEntityByNetworkID(id);
            if (!entity || replication.GetViewer(entity->ownerGUID) != entity) {
                return FailError(call, name + ": " + std::to_string(id) + " is not a player's avatar");
            }
            guid = entity->ownerGUID;
            return true;
        }

        bool ResolvePlayers(Bind::Call &call, const Networking::Replication::ReplicationManager &replication, const std::vector<uint64_t> &ids, const std::string &name, std::vector<MafiaNet::PeerGuid> &out) {
            out.resize(ids.size());
            for (size_t i = 0; i < ids.size(); ++i) {
                if (!ResolvePlayer(call, replication, ids[i], name, out[i])) {
                    return false;
                }
            }
            return true;
        }

        bool CheckViewers(Bind::Call &call, const Networking::Replication::ReplicationManager &replication, Options &options) {
            const bool showTo = options.showTo && options.showTo->has_value();
            return (!showTo || ResolvePlayers(call, replication, **options.showTo, "showTo", options.showToGuids)) && (!options.hideFrom || ResolvePlayers(call, replication, *options.hideFrom, "hideFrom", options.hideFromGuids));
        }

        // hideForTarget: the player the label follows is not sent it.
        void HideFromTarget(Networking::Replication::ReplicationManager &replication, Networking::Replication::Entities::TextLabelEntity &label, const Networking::Replication::NetworkEntity *target) {
            MafiaNet::PeerGuid hidden = MafiaNet::UNASSIGNED_PEER_GUID;
            if (label.hideForTarget && target && replication.GetViewer(target->ownerGUID) == target) {
                hidden = target->ownerGUID;
            }
            // Runs every tick for an attached label, so nothing is copied unless it changed.
            const auto *viewers = label.streaming.viewers.get();
            if ((viewers ? viewers->exceptFollowed : MafiaNet::UNASSIGNED_PEER_GUID) != hidden) {
                Networking::Replication::ViewerFilter next = viewers ? *viewers : Networking::Replication::ViewerFilter {};
                next.exceptFollowed                        = hidden;
                replication.SetViewerFilter(label, std::move(next));
            }
        }

        // An attached label streams from where its target is, in the target's virtual world.
        void Follow(Networking::Replication::ReplicationManager &replication, Networking::Replication::Entities::TextLabelEntity &label, const Networking::Replication::NetworkEntity &target) {
            label.position = target.position;
            if (label.GetVirtualWorld() != target.GetVirtualWorld()) {
                label.SetVirtualWorld(target.GetVirtualWorld());
            }
            HideFromTarget(replication, label, &target);
        }

        // Replaces the showTo and/or hideFrom halves of a label's viewer filter.
        void ApplyViewers(Networking::Replication::ReplicationManager &replication, Networking::Replication::Entities::TextLabelEntity &label, const Options &options) {
            const auto *viewers                        = label.streaming.viewers.get();
            Networking::Replication::ViewerFilter next = viewers ? *viewers : Networking::Replication::ViewerFilter {};
            if (options.showTo) {
                if (options.showTo->has_value()) {
                    next.only.emplace(options.showToGuids.begin(), options.showToGuids.end());
                }
                else {
                    next.only.reset();
                }
            }
            if (options.hideFrom) {
                next.except = std::unordered_set<MafiaNet::PeerGuid>(options.hideFromGuids.begin(), options.hideFromGuids.end());
            }
            replication.SetViewerFilter(label, std::move(next));
        }

        void ApplyOptions(TextLabel &handle, Networking::Replication::Entities::TextLabelEntity &label, const Options &options) {
            if (options.text) {
                handle.SetText(*options.text);
            }
            if (options.markup) {
                label.markup = *options.markup;
            }
            if (options.position) {
                (label.attachNetworkId != 0 ? label.ownPosition : label.position) = *options.position;
            }
            if (options.attachTo) {
                if (*options.attachTo == 0) {
                    handle.Detach();
                }
                else {
                    // Checked by the caller before anything was applied.
                    handle.AttachTo(*options.attachTo, options.offset.value_or(label.attachOffset));
                }
            }
            else if (options.offset) {
                label.attachOffset = *options.offset;
            }
            if (options.fontSize) {
                handle.SetFontSize(*options.fontSize);
            }
            if (options.sizeMode) {
                label.sizeMode = *options.sizeMode;
            }
            if (options.minSize) {
                label.minSize = *options.minSize;
            }
            if (options.maxSize) {
                label.maxSize = *options.maxSize;
            }
            if (options.color) {
                label.color = *options.color;
            }
            if (options.opacity) {
                label.opacity = *options.opacity;
            }

            // A preset first, so explicit blocks in the same call refine it.
            if (options.style) {
                Networking::Replication::Entities::ApplyTextLabelStylePreset(label, *options.style);
            }
            if (options.outline) {
                const Options::Outline outline = options.outline->value_or(Options::Outline {0.0f});
                label.outlineWidth             = outline.width;
                label.outlineColor             = outline.color;
            }
            if (options.shadow) {
                const Options::Shadow shadow = options.shadow->value_or(Options::Shadow {0});
                label.shadowColor            = shadow.color;
                label.shadowOffsetX          = shadow.offsetX;
                label.shadowOffsetY          = shadow.offsetY;
                label.shadowBlur             = shadow.blur;
            }
            if (options.background) {
                const Options::Background background = options.background->value_or(Options::Background {0});
                label.backgroundColor                = background.color;
                label.backgroundPadding              = background.padding;
                label.backgroundRadius               = background.radius;
            }
            if (options.style || options.outline || options.shadow || options.background) {
                label.style = Networking::Replication::Entities::NearestTextLabelStylePreset(label);
            }

            if (options.align) {
                label.align = *options.align;
            }
            if (options.anchor) {
                label.anchor = *options.anchor;
            }
            if (options.lineSpacing) {
                label.lineSpacing = *options.lineSpacing;
            }
            if (options.maxWidth) {
                label.maxWidth = *options.maxWidth;
            }
            if (options.drawDistance) {
                handle.SetDrawDistance(*options.drawDistance);
            }
            if (options.fadeDistance) {
                handle.SetFadeDistance(*options.fadeDistance);
            }
            if (options.occlusion) {
                label.occlusion = *options.occlusion;
            }
            if (options.virtualWorld) {
                if (label.attachNetworkId != 0) {
                    label.ownVirtualWorld = *options.virtualWorld;
                }
                else {
                    handle.SetVirtualWorld(*options.virtualWorld);
                }
            }
            if (options.showTo || options.hideFrom) {
                ApplyViewers(*CoreModules::GetReplication(), label, options);
            }
            // After attachTo, so the player it hides from is the one it now follows.
            if (options.hideForTarget) {
                handle.SetHideForTarget(*options.hideForTarget);
            }
        }

        // A filter's players as their avatars' network ids, ascending. A connected player with no avatar
        // right now has no id to show: they stay in the filter but are left out here.
        Bind::Value PlayerIdArray(Bind::Call &call, const Networking::Replication::ReplicationManager &replication, const std::unordered_set<MafiaNet::PeerGuid> &guids) {
            std::vector<uint64_t> ids;
            for (const MafiaNet::PeerGuid guid : guids) {
                if (auto *viewer = replication.GetViewer(guid)) {
                    ids.push_back(viewer->GetNetworkID());
                }
            }
            std::sort(ids.begin(), ids.end());
            Bind::Value array = call.NewArray();
            for (const uint64_t id : ids) {
                array.Append(call.Integer(static_cast<int64_t>(id)));
            }
            return array;
        }

        template <typename T, size_t N>
        std::string EnumName(T value, const std::pair<std::string_view, T> (&entries)[N]) {
            for (const auto &[label, entry] : entries) {
                if (entry == value) {
                    return std::string(label);
                }
            }
            return std::string(entries[0].first);
        }

        // Who the label is for, as getAudience() reports it. A showTo of everyone is null, and so no key in Lua.
        Bind::Value AudienceObject(Bind::Call &call, const Networking::Replication::ReplicationManager &replication, const Networking::Replication::Entities::TextLabelEntity &label) {
            const auto *viewers = label.streaming.viewers.get();
            Bind::Value out     = call.NewObject();
            out.Set("showTo", viewers && viewers->only ? PlayerIdArray(call, replication, *viewers->only) : call.Null());
            out.Set("hideFrom", viewers ? PlayerIdArray(call, replication, viewers->except) : call.NewArray());
            out.Set("hideForTarget", call.Boolean(label.hideForTarget));
            return out;
        }

        // The label as an options object: what update() takes, so one label's look can seed another's.
        // The audience stays out of it: applying this to another label, or later, must not change who
        // can see that label. A label that is not attached has a null attachTo, and so no key in Lua.
        Bind::Value OptionsObject(Bind::Call &call, const Networking::Replication::Entities::TextLabelEntity &label, uint32_t virtualWorld) {
            const auto vec = [&](const glm::vec3 &v) {
                Bind::Value object = call.NewObject();
                object.Set("x", call.Number(v.x));
                object.Set("y", call.Number(v.y));
                object.Set("z", call.Number(v.z));
                return object;
            };

            Bind::Value out = call.NewObject();
            out.Set("text", call.String(label.text));
            out.Set("markup", call.Boolean(label.markup));
            out.Set("position", vec(label.attachNetworkId != 0 ? label.ownPosition : label.position));
            out.Set("attachTo", label.attachNetworkId != 0 ? call.Integer(static_cast<int64_t>(label.attachNetworkId)) : call.Null());
            out.Set("offset", vec(label.attachOffset));
            out.Set("fontSize", call.Number(label.fontSize));
            out.Set("sizeMode", call.String(EnumName(label.sizeMode, kSizeModes)));
            out.Set("minSize", call.Number(label.minSize));
            out.Set("maxSize", call.Number(label.maxSize));
            out.Set("color", call.Integer(label.color));
            out.Set("opacity", call.Number(label.opacity));
            out.Set("style", call.String(EnumName(label.style, kStyles)));

            if (label.outlineWidth > 0.0f) {
                Bind::Value outline = call.NewObject();
                outline.Set("width", call.Number(label.outlineWidth));
                outline.Set("color", call.Integer(label.outlineColor));
                out.Set("outline", outline);
            }
            else {
                out.Set("outline", call.Boolean(false));
            }
            if ((label.shadowColor >> 24) != 0) {
                Bind::Value shadow = call.NewObject();
                shadow.Set("color", call.Integer(label.shadowColor));
                shadow.Set("offsetX", call.Number(label.shadowOffsetX));
                shadow.Set("offsetY", call.Number(label.shadowOffsetY));
                shadow.Set("blur", call.Number(label.shadowBlur));
                out.Set("shadow", shadow);
            }
            else {
                out.Set("shadow", call.Boolean(false));
            }
            if ((label.backgroundColor >> 24) != 0) {
                Bind::Value background = call.NewObject();
                background.Set("color", call.Integer(label.backgroundColor));
                background.Set("padding", call.Number(label.backgroundPadding));
                background.Set("radius", call.Number(label.backgroundRadius));
                out.Set("background", background);
            }
            else {
                out.Set("background", call.Boolean(false));
            }

            out.Set("align", call.String(EnumName(label.align, kAligns)));
            out.Set("anchor", call.String(EnumName(label.anchor, kAnchors)));
            out.Set("lineSpacing", call.Number(label.lineSpacing));
            out.Set("maxWidth", call.Number(label.maxWidth));
            out.Set("drawDistance", call.Number(label.drawDistance));
            out.Set("fadeDistance", call.Number(label.fadeDistance));
            out.Set("occlusion", call.Boolean(label.occlusion));
            out.Set("virtualWorld", call.Integer(virtualWorld));
            return out;
        }

        // ToInt32 of JavaScript, which is how the components of setColor were always read: the whole part, wrapped
        // to 32 bits.
        int ToInt32(double number) {
            if (!std::isfinite(number)) {
                return 0;
            }
            const double wrapped = std::fmod(std::trunc(number), 4294967296.0);
            return static_cast<int>(static_cast<uint32_t>(static_cast<int64_t>(wrapped)));
        }

        // setColor(color | r, g?, b?, a?). It takes either a Color or byte components (r, g, b, a from 0-255), so
        // scripts don't have to hand-pack this API's ARGB layout.
        void CallSetColor(Bind::Call &call) {
            auto *self = call.Self<TextLabel>();
            if (!self) {
                return;
            }
            if (const auto *color = call.Arg(0).AsBound<Color>()) {
                self->SetColorPacked(color->toARGB());
                return;
            }
            const auto num = [&](size_t i, int fallback) {
                return call.Arg(i).IsNumber() ? ToInt32(call.Arg(i).AsNumber()) : fallback;
            };
            self->SetColor(num(0, 0), num(1, 0), num(2, 0), num(3, 255));
        }

        void CallUpdate(Bind::Call &call) {
            auto *self = call.Self<TextLabel>();
            if (!self || !self->ResolveLabel()) {
                return;
            }
            if (!IsObject(call.Arg(0))) {
                Fail(call, "update expects an options object");
                return;
            }
            Options options;
            if (!ParseOptions(call, call.Arg(0), TextLabel::GetUnitsPerMetre(), options)) {
                return;
            }
            // Parsing ran the options' getters, which can destroy the label; look it up only now.
            auto *label = self->ResolveLabel();
            if (label && CheckTarget(call, options, self) && CheckSizes(call, options, label) && CheckViewers(call, *CoreModules::GetReplication(), options)) {
                ApplyOptions(*self, *label, options);
            }
        }

        void CallSetVisibleFor(Bind::Call &call) {
            auto *self = call.Self<TextLabel>();
            if (!self || !self->ResolveLabel()) {
                return;
            }
            if (!call.Arg(1).IsBoolean()) {
                Fail(call, "setVisibleFor expects (player, visible)");
                return;
            }
            const auto player       = PlayerId(call, call.Arg(0), "player");
            MafiaNet::PeerGuid guid = MafiaNet::UNASSIGNED_PEER_GUID;
            if (!player || !ResolvePlayer(call, *CoreModules::GetReplication(), *player, "setVisibleFor", guid)) {
                return;
            }
            self->SetVisibleFor(guid, call.Arg(1).AsBoolean());
        }

        void CallAttachTo(Bind::Call &call) {
            auto *self = call.Self<TextLabel>();
            if (!self || !self->ResolveLabel()) {
                return;
            }
            // null detaches, as 0 does. Lua, which has no null, has 0 and detach().
            const auto target = Target(call, call.Arg(0), "target", call.Arg(0).IsNull());
            if (!target) {
                return;
            }
            std::optional<glm::vec3> offset = glm::vec3(0.0f);
            if (!call.Arg(1).IsNil() || call.Arg(1).IsNull()) {
                offset = Vec3(call, call.Arg(1), "offset");
                if (!offset) {
                    return;
                }
            }
            if (*target == 0) {
                self->Detach();
            }
            else if (!self->AttachTo(*target, *offset)) {
                Fail(call, "attachTo must be another live entity");
            }
        }

        // null for a label that is gone.
        void CallGetOptions(Bind::Call &call) {
            const auto *self  = call.Self<TextLabel>();
            const auto *label = self ? self->ResolveLabel() : nullptr;
            if (!label) {
                call.Return(call.Null());
                return;
            }
            call.Return(OptionsObject(call, *label, label->attachNetworkId != 0 ? label->ownVirtualWorld : self->GetVirtualWorld()));
        }

        void CallGetAudience(Bind::Call &call) {
            const auto *self  = call.Self<TextLabel>();
            const auto *label = self ? self->ResolveLabel() : nullptr;
            if (!label) {
                call.Return(call.Null());
                return;
            }
            call.Return(AudienceObject(call, *CoreModules::GetReplication(), *label));
        }

        // TextLabel.create(x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?) -> TextLabel
        void CallCreate(Bind::Call &call) {
            if (call.ArgCount() < 4 || !call.Arg(0).IsNumber() || !call.Arg(1).IsNumber() || !call.Arg(2).IsNumber() || !call.Arg(3).IsString()) {
                call.ThrowTypeError("TextLabel.create: expected (x, y, z, text, fontSize? | options?, drawDistance?, virtualWorld?)");
                return;
            }

            // Options are checked before anything exists, so a bad one leaves nothing half-made.
            Options options;
            const bool withOptions = IsObject(call.Arg(4));
            if (withOptions && (!ParseOptions(call, call.Arg(4), TextLabel::GetUnitsPerMetre(), options) || !CheckSizes(call, options, nullptr))) {
                return;
            }

            auto *replication = CoreModules::GetReplication();
            if (!replication) {
                call.ThrowError("TextLabel.create: replication unavailable");
                return;
            }

            if (!CheckTarget(call, options, nullptr) || !CheckViewers(call, *replication, options)) {
                return;
            }

            int count = 0;
            replication->ForEach<TextLabel::LabelEntity>([&count](TextLabel::LabelEntity *) {
                ++count;
            });
            if (count >= TextLabel::kMaxLabels) {
                call.ThrowError("TextLabel.create: label limit reached");
                return;
            }

            auto *label = replication->CreateEntity<TextLabel::LabelEntity>();
            if (!label) {
                call.ThrowError("TextLabel.create: failed to create entity (type not registered?)");
                return;
            }

            // A resource owns the labels it makes, and they go when it stops. The owner is the resource that is
            // calling, named by the call: the resource of the isolate or of the Lua state the script runs in. So a
            // label made at load, from a timer or from a handler is the calling resource's, and one an export of B
            // makes while A calls it is B's.
            //
            // The server also tells the resource manager of every entity made (ResourceManager::OnEntityCreated),
            // which names the same resource for JavaScript, from the isolate that is running, and none for Lua
            // outside the load of a script, where no JavaScript runs. This call is what makes a Lua resource the
            // owner, and what makes a JavaScript one the owner where a host wired no such hook.
            if (auto *manager = call.GetResourceManager()) {
                manager->AssignEntity(call.ResourceName(), label->GetNetworkID());
            }

            label->position = glm::vec3(static_cast<float>(call.Arg(0).AsNumber()), static_cast<float>(call.Arg(1).AsNumber()), static_cast<float>(call.Arg(2).AsNumber()));

            TextLabel handle(label->GetNetworkID());
            handle.SetText(call.Arg(3).AsString());
            handle.SetDrawDistance(TextLabel::kDefaultMetres * TextLabel::GetUnitsPerMetre());

            if (withOptions) {
                ApplyOptions(handle, *label, options);
            }
            else {
                if (call.Arg(4).IsNumber()) {
                    handle.SetFontSize(static_cast<float>(call.Arg(4).AsNumber()));
                }
                if (call.Arg(5).IsNumber()) {
                    handle.SetDrawDistance(static_cast<float>(call.Arg(5).AsNumber()));
                }
                if (call.Arg(6).IsNumber()) {
                    handle.SetVirtualWorld(static_cast<uint32_t>(call.Arg(6).AsNumber()));
                }
            }

            call.Return(call.Wrap(std::move(handle)));
        }
    } // namespace

    void TextLabel::SetUnitsPerMetre(float unitsPerMetre) {
        if (std::isfinite(unitsPerMetre) && unitsPerMetre > 0.0f) {
            _unitsPerMetre = unitsPerMetre;
        }
    }

    float TextLabel::GetUnitsPerMetre() {
        return _unitsPerMetre;
    }

    TextLabel::LabelEntity *TextLabel::ResolveLabel() const {
        return dynamic_cast<LabelEntity *>(Resolve());
    }

    std::string TextLabel::GetText() const {
        auto *label = ResolveLabel();
        return label ? label->text : std::string();
    }

    void TextLabel::SetText(const std::string &text) {
        if (auto *label = ResolveLabel()) {
            label->text = text;
            Utils::StringUtils::TruncateUtf8(label->text, kMaxTextBytes);
        }
    }

    uint32_t TextLabel::GetColor() const {
        auto *label = ResolveLabel();
        return label ? label->color : 0xFFFFFFFF;
    }

    void TextLabel::SetColor(int r, int g, int b, int a) {
        const auto clamp = [](int c) {
            return static_cast<uint32_t>(std::clamp(c, 0, 255));
        };
        SetColorPacked((clamp(a) << 24) | (clamp(r) << 16) | (clamp(g) << 8) | clamp(b));
    }

    void TextLabel::SetColorPacked(uint32_t argb) {
        if (auto *label = ResolveLabel()) {
            label->color = argb;
        }
    }

    float TextLabel::GetFontSize() const {
        auto *label = ResolveLabel();
        return label ? label->fontSize : 0.0f;
    }

    void TextLabel::SetFontSize(float size) {
        if (auto *label = ResolveLabel()) {
            label->fontSize = std::clamp(size, kMinFontSize, kMaxFontSize);
        }
    }

    float TextLabel::GetDrawDistance() const {
        auto *label = ResolveLabel();
        return label ? label->drawDistance : 0.0f;
    }

    // The draw distance is also the label's streaming radius, so a game that holds the type to its
    // entities' own range sends a label only to players who could see it.
    void TextLabel::SetDrawDistance(float distance) {
        if (auto *label = ResolveLabel()) {
            label->drawDistance    = std::clamp(distance, 1.0f, kMaxDrawMetres * _unitsPerMetre);
            label->streaming.range = label->drawDistance;
        }
    }

    float TextLabel::GetFadeDistance() const {
        auto *label = ResolveLabel();
        return label ? label->fadeDistance : 0.0f;
    }

    void TextLabel::SetFadeDistance(float distance) {
        if (auto *label = ResolveLabel()) {
            label->fadeDistance = std::max(distance, 0.0f);
        }
    }

    uint32_t TextLabel::GetStyle() const {
        auto *label = ResolveLabel();
        return label ? static_cast<uint32_t>(label->style) : 0;
    }

    void TextLabel::SetStyle(uint32_t style) {
        if (auto *label = ResolveLabel()) {
            if (style <= static_cast<uint32_t>(LabelStyle::Box)) {
                Networking::Replication::Entities::ApplyTextLabelStylePreset(*label, static_cast<LabelStyle>(style));
            }
        }
    }

    bool TextLabel::CanAttachTo(uint64_t networkId) const {
        auto *replication = CoreModules::GetReplication();
        return replication && networkId != 0 && networkId != _id && replication->GetEntityByNetworkID(networkId) != nullptr;
    }

    bool TextLabel::AttachTo(uint64_t networkId, const glm::vec3 &offset) {
        auto *label = ResolveLabel();
        if (!label || !CanAttachTo(networkId)) {
            return false;
        }
        if (label->attachNetworkId == 0) {
            label->ownPosition     = label->position;
            label->ownVirtualWorld = label->GetVirtualWorld();
        }
        label->attachNetworkId = networkId;
        label->attachOffset    = offset;
        _attached.insert(_id);
        // From where the target is now, rather than from the next tick.
        auto *replication = CoreModules::GetReplication();
        Follow(*replication, *label, *replication->GetEntityByNetworkID(networkId));
        return true;
    }

    void TextLabel::Detach() {
        auto *label = ResolveLabel();
        if (!label || label->attachNetworkId == 0) {
            return;
        }
        label->attachNetworkId = 0;
        label->attachOffset    = glm::vec3(0.0f);
        label->position        = label->ownPosition;
        label->SetVirtualWorld(label->ownVirtualWorld);
        _attached.erase(_id);
        HideFromTarget(*CoreModules::GetReplication(), *label, nullptr);
    }

    void TextLabel::SetVisibleFor(MafiaNet::PeerGuid guid, bool visible) {
        auto *label = ResolveLabel();
        if (!label) {
            return;
        }
        CoreModules::GetReplication()->SetViewerAdmitted(*label, guid, visible);
    }

    void TextLabel::SetHideForTarget(bool hide) {
        auto *label = ResolveLabel();
        if (!label) {
            return;
        }
        auto *replication    = CoreModules::GetReplication();
        label->hideForTarget = hide;
        HideFromTarget(*replication, *label, label->attachNetworkId != 0 ? replication->GetEntityByNetworkID(label->attachNetworkId) : nullptr);
    }

    void TextLabel::FollowTargets(Networking::Replication::ReplicationManager &replication) {
        std::vector<Networking::Replication::NetworkEntity *> orphans;
        for (auto it = _attached.begin(); it != _attached.end();) {
            auto *label = dynamic_cast<LabelEntity *>(replication.GetEntityByNetworkID(*it));
            if (!label || label->attachNetworkId == 0) {
                it = _attached.erase(it);
                continue;
            }
            auto *target = replication.GetEntityByNetworkID(label->attachNetworkId);
            if (!target) {
                orphans.push_back(label);
                it = _attached.erase(it);
                continue;
            }
            Follow(replication, *label, *target);
            ++it;
        }
        // A label lives as long as the entity it follows: a name tag must not outstay its player.
        for (auto *label : orphans) {
            replication.DestroyEntity(label);
        }
    }

    std::string TextLabel::ToString() const {
        std::ostringstream ss;
        ss << "TextLabel{ id: " << _id << ", text: \"" << GetText() << "\" }";
        return ss.str();
    }

    void TextLabel::Destroy() {
        auto *label = ResolveLabel();
        if (!label) {
            return;
        }
        if (auto *replication = CoreModules::GetReplication()) {
            replication->DestroyEntity(label);
        }
    }

    const Bind::Class<TextLabel> &TextLabel::Definition() {
        static const auto definition = [] {
            // Inherit looks the definition of the base up.
            Entity::Definition();

            const auto options = Framework::Scripting::Bind::Type::Custom("TextLabelOptions", "table");
            const auto vector  = Framework::Scripting::Bind::Type::Custom("Vector3 | { x: number; y: number; z: number }", "Vector3|{ x: number, y: number, z: number }");
            return Bind::Class<TextLabel>("TextLabel", "Server-created replicated world-space text label.")
                .Inherit<Entity>()
                .Constructor<uint64_t>(Framework::Scripting::Bind::Doc("Creates a wrapper for an existing text label; use TextLabel.create to spawn one.").Param("id", "Existing text-label network entity identifier."))
                .Method("toString", &TextLabel::ToString, Framework::Scripting::Bind::Doc("Formats this label for logging and debugging.").Returns("Text containing the label ID and current text."))
                .Method("destroy", &TextLabel::Destroy, Framework::Scripting::Bind::Doc("Destroys this replicated label; stale wrappers no longer resolve afterward."))
                .Method("setText", &TextLabel::SetText, Framework::Scripting::Bind::Doc("Changes the label's displayed text.").Param("text", "Replacement text replicated to clients; at most 1024 bytes."))
                .Method("setFontSize", &TextLabel::SetFontSize, Framework::Scripting::Bind::Doc("Changes the rendered font size.").Param("size", "Font size in pixels, clamped from 1 to 512."))
                .Method("setDrawDistance", &TextLabel::SetDrawDistance, Framework::Scripting::Bind::Doc("Changes how far away clients can render the label.").Param("distance", "Maximum rendering distance in world units, clamped from 1 to 1000 metres' worth."))
                .Method("setFadeDistance", &TextLabel::SetFadeDistance, Framework::Scripting::Bind::Doc("Changes where the label starts fading out.").Param("distance", "Distance in world units where fading starts; 0 starts it at 75% of the draw distance."))
                .Method("setStyle", &TextLabel::SetStyle, Framework::Scripting::Bind::Doc("Resets the outline, shadow and background to a preset.").Param("style", "0 none, 1 shadow, 2 outline, 3 box; unsupported values are ignored."))
                .Method("detach", &TextLabel::Detach, Framework::Scripting::Bind::Doc("Stops following an entity; the label returns to its own position."))
                .Property("text", &TextLabel::GetText, "Current replicated label text.")
                .Property("color", &TextLabel::GetColor, "Current packed ARGB color.")
                .Property("fontSize", &TextLabel::GetFontSize, "Current font size.")
                .Property("drawDistance", &TextLabel::GetDrawDistance, "Current maximum rendering distance in world units.")
                .Property("fadeDistance", &TextLabel::GetFadeDistance, "Distance in world units where fading starts.")
                .Property("style", &TextLabel::GetStyle, "The preset nearest the current look: 0 none, 1 shadow, 2 outline, 3 box.")
                // A context function, as are the ones below: a signature cannot say Color | number, an options
                // object or a player given as a handle or an id.
                .Method("setColor", &CallSetColor,
                    Framework::Scripting::Bind::Doc("Changes the label's packed ARGB color from a Color or byte components.")
                        .Param("colorOrR", Framework::Scripting::Bind::Type::Custom("Color | number", "Color|integer"), "A Color, or the red byte (0-255) when passing components.")
                        .OptionalParam("g", Framework::Scripting::Bind::TypeOf<int>(), "Green byte (0-255) when passing components.")
                        .OptionalParam("b", Framework::Scripting::Bind::TypeOf<int>(), "Blue byte (0-255) when passing components.")
                        .OptionalParam("a", Framework::Scripting::Bind::TypeOf<int>(), "Alpha byte (0-255) when passing components; defaults to 255."))
                .Method("update", &CallUpdate, Framework::Scripting::Bind::Doc("Changes any part of the label at once. A bad key throws and changes nothing.").Param("options", options, "Only the keys given change; outline, shadow, background, showTo and hideFrom are replaced whole."))
                .Method("attachTo", &CallAttachTo,
                    Framework::Scripting::Bind::Doc("Follows another live entity instead of staying at the label's own position. It streams from where the target is, moves into its virtual world and is destroyed with it.")
                        .Param("target", Framework::Scripting::Bind::Type::Custom("Entity | number", "Entity|integer"), "The entity to follow, or its network id.")
                        .OptionalParam("offset", vector, "Where to draw relative to it; the game decides the reference point (a player's head, say)."))
                .Method("getOptions", &CallGetOptions, Framework::Scripting::Bind::Doc("The label's current look and placement, in the shape update() takes. The audience is left out: see getAudience().").ReturnType(options))
                .Method("getAudience", &CallGetAudience,
                    Framework::Scripting::Bind::Doc("Who the label is for, with players as network ids. For reading: a connected player with no avatar right now has no id and is left out, though the label stays as set for them, so passing this to update() would drop them.")
                        .ReturnType(Framework::Scripting::Bind::Type::Custom("{ showTo: number[] | null; hideFrom: number[]; hideForTarget: boolean }", "{ showTo: integer[]?, hideFrom: integer[], hideForTarget: boolean }")))
                .Method("setVisibleFor", &CallSetVisibleFor,
                    Framework::Scripting::Bind::Doc("Shows or hides the label for one player. A hidden player's client is never sent it; hideForTarget still applies.")
                        .Param("player", Framework::Scripting::Bind::Type::Custom("Player | number", "Player|integer"), "A connected player, or their network id.")
                        .Param("visible", Framework::Scripting::Bind::TypeOf<bool>(), "false adds them to hideFrom and takes them off showTo; true undoes both, adding them to showTo while it is set."))
                .Static("create", &CallCreate,
                    Framework::Scripting::Bind::Doc("Creates and replicates a world-space text label, up to the server-wide limit of 512. A bad option throws and creates nothing.")
                        .Param("x", Framework::Scripting::Bind::TypeOf<double>(), "World-space X coordinate.")
                        .Param("y", Framework::Scripting::Bind::TypeOf<double>(), "World-space Y coordinate.")
                        .Param("z", Framework::Scripting::Bind::TypeOf<double>(), "World-space Z coordinate.")
                        .Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Initial displayed text.")
                        .OptionalParam("fontSizeOrOptions", Framework::Scripting::Bind::Type::Custom("number | TextLabelOptions", "number|table"), "Look and placement, or (the older form) the font size from 1 to 512.")
                        .OptionalParam("drawDistance", Framework::Scripting::Bind::TypeOf<double>(), "Older form only: the draw distance in world units.")
                        .OptionalParam("virtualWorld", Framework::Scripting::Bind::TypeOf<uint32_t>(), "Older form only: the virtual-world identifier.")
                        .ReturnType(Framework::Scripting::Bind::Type::Class("TextLabel"))
                        .Returns("Handle for the newly created label."))
                // Crosses between runtimes as its network id.
                .Transfer(&TextLabel::GetId);
        }();
        return definition;
    }

    v8pp::class_<TextLabel> &TextLabel::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            // v8pp inherit<Entity> requires Entity registered first.
            Entity::GetClass(isolate);
            return Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
        });
    }

    void TextLabel::RegisterOptionsType(v8::Isolate *isolate) {
        auto &type          = GetScriptingCatalog(isolate).data_type("TextLabelOptions", "A text label's look, placement and audience. Every key is optional; colours are 0xAARRGGBB numbers, \"#RRGGBB[AA]\" strings or Colors.");
        const auto optional = [](const char *name) {
            return v8pp::metadata::type {name, {}, true};
        };
        type.add_property("text", optional("string"), "UTF-8 text, at most 1024 bytes; '\\n' breaks lines.");
        type.add_property("markup", optional("boolean"), "Parse inline tags: {#RRGGBB} / {#RRGGBBAA} colour, {#} back to `color`, {b}{/b}, {i}{/i}, {{ for a brace. Default false.");
        type.add_property("position", optional("Vector3 | { x: number; y: number; z: number }"), "Where the label stands while it is not attached, and returns to when detached.");
        type.add_property("attachTo", optional("Entity | number | null"), "Another live entity to follow, or its network id; null or 0 detaches. The label is destroyed with it.");
        type.add_property("offset", optional("Vector3 | { x: number; y: number; z: number }"), "Relative to the attached entity; the game decides the reference point (a player's head, say).");
        type.add_property("fontSize", optional("number"), "Pixels, 1-512. Default 16.");
        type.add_property("sizeMode", optional("\"screen\" | \"world\""), "\"screen\" (default): fontSize pixels at any distance. \"world\": fontSize pixels at one metre on a 1080p view, shrinking with distance.");
        type.add_property("minSize", optional("number"), "World mode: never smaller than this many pixels.");
        type.add_property("maxSize", optional("number"), "World mode: never larger than this many pixels; 0 = no cap.");
        type.add_property("color", optional("number | string | Color"), "Text colour. Default white.");
        type.add_property("opacity", optional("number"), "0-1, multiplies every colour. Default 1.");
        type.add_property("style", optional("\"none\" | \"shadow\" | \"outline\" | \"box\" | number"), "A preset for outline, shadow and background; blocks in the same call refine it.");
        type.add_property("outline", optional("boolean | null | { color?: number | string | Color; width?: number }"), "true is 1 px black; width is at most 4.");
        type.add_property("shadow", optional("boolean | null | { color?: number | string | Color; offsetX?: number; offsetY?: number; blur?: number }"), "A drop shadow; blur (at most 8) is approximated. Default a 1 px shadow.");
        type.add_property("background", optional("boolean | null | { color?: number | string | Color; padding?: number; radius?: number }"), "A plate behind the text; true is translucent black, 4 px padding and radius.");
        type.add_property("align", optional("\"left\" | \"center\" | \"right\""), "How lines align with each other. Default \"center\".");
        type.add_property("anchor", optional("\"center\" | \"bottom\" | \"top\""), "Which part of the text block sits on the point. \"bottom\" grows upward, as a tag over a head.");
        type.add_property("lineSpacing", optional("number"), "Line height as a multiple of the size. Default 1.");
        type.add_property("maxWidth", optional("number"), "Wraps at spaces past this width, in fontSize pixels. 0 (default) never wraps.");
        type.add_property("drawDistance", optional("number"), "World units; the label neither draws nor streams past it. Default 30 metres' worth.");
        type.add_property("fadeDistance", optional("number"), "World units where fading starts; 0 starts at 75% of drawDistance.");
        type.add_property("occlusion", optional("boolean"), "Hidden behind world geometry. Default true.");
        type.add_property("virtualWorld", optional("number"), "The virtual world it lives in while it is not attached, and returns to when detached.");
        type.add_property("showTo", optional("(Player | number)[] | null"),
            "Only these players are sent it; [] is nobody, null (default) everyone. hideFrom wins over it. Lasts for the connection: a player who reconnects is off it until added again. Read back through getAudience().");
        type.add_property("hideFrom", optional("(Player | number)[] | null"), "These players are not sent it; null or [] clears. Lasts for the connection: a player who reconnects sees it again until hidden again. Read back through getAudience().");
        type.add_property("hideForTarget", optional("boolean"), "Not sent to the player it is attached to, as for their own name tag. Default false.");
    }

    void TextLabel::Register(Bind::Registrar &registrar, Bind::Languages languages) {
        Entity::Register(registrar, languages & Bind::Languages::Lua);
        // What an option reads a colour from, whether or not the state has it as a global.
        registrar.Type(Color::Definition(), languages);
        if (registrar.HasJavaScript(languages)) {
            GetClass(registrar.Isolate());
            RegisterOptionsType(registrar.Isolate());
        }
        registrar.Global(Definition(), languages);
    }

    void TextLabel::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }
} // namespace Framework::Scripting::Builtins
