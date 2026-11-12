/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "nametags.h"

#include <scripting/builtins/script_args.h>

#include <external/imgui/widgets/nametag_view.h>
#include <integrations/client/ui/nametag_notes.h>
#include <scripting/lua/lua_runtime.h>
#include <scripting/scripting_catalog.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {

    namespace {
        constexpr size_t kMaxLabelBytes = 512;

        // Back the cap off any UTF-8 continuation byte: a split glyph renders as garbage.
        std::string ClampLabel(std::string text) {
            if (text.size() <= kMaxLabelBytes) {
                return text;
            }
            size_t cap = kMaxLabelBytes;
            while (cap > 0 && (static_cast<unsigned char>(text[cap]) & 0xC0) == 0x80) {
                --cap;
            }
            text.resize(cap);
            return text;
        }

        // A packed colour: the whole part of the number, wrapped to 32 bits. 0 for what is not finite.
        uint32_t ToColor(double number) {
            if (!std::isfinite(number)) {
                return 0;
            }
            return static_cast<uint32_t>(static_cast<int64_t>(std::fmod(std::trunc(number), 4294967296.0)));
        }

        // Sets one of the viewer's switches from the flag a script passes. `expected` is thrown without one.
        void SetSwitch(Framework::Scripting::Bind::Call &call, bool &target, const char *expected) {
            if (call.ArgCount() < 1) {
                call.ThrowError(expected);
                return;
            }
            target = Framework::Scripting::Builtins::ReadFlag(call, 0);
        }

        void SetVisible(Framework::Scripting::Bind::Call &call) {
            SetSwitch(call, Framework::External::ImGUI::Widgets::NameTagView::showTags, "Nametags.setVisible: expected (visible)");
        }

        void IsVisible(Framework::Scripting::Bind::Call &call) {
            call.Return(Framework::External::ImGUI::Widgets::NameTagView::showTags);
        }

        void SetHealthVisible(Framework::Scripting::Bind::Call &call) {
            SetSwitch(call, Framework::External::ImGUI::Widgets::NameTagView::showHealth, "Nametags.setHealthVisible: expected (visible)");
        }

        void IsHealthVisible(Framework::Scripting::Bind::Call &call) {
            call.Return(Framework::External::ImGUI::Widgets::NameTagView::showHealth);
        }

        void SetSelfVisible(Framework::Scripting::Bind::Call &call) {
            SetSwitch(call, Framework::External::ImGUI::Widgets::NameTagView::showSelf, "Nametags.setSelfVisible: expected (visible)");
        }

        void IsSelfVisible(Framework::Scripting::Bind::Call &call) {
            call.Return(Framework::External::ImGUI::Widgets::NameTagView::showSelf);
        }

        void SetLabel(Framework::Scripting::Bind::Call &call) {
            if (!call.Arg(0).IsNumber() || !call.Arg(1).IsString()) {
                call.ThrowError("Nametags.setLabel: expected (entityId, text, durationMs?, color?)");
                return;
            }
            const auto id       = static_cast<uint64_t>(Framework::Scripting::Builtins::ToInt64(call.Arg(0)));
            const auto duration = call.Arg(2).IsNumber() ? static_cast<float>(call.Arg(2).AsNumber()) : 6000.0f;
            const auto color    = call.Arg(3).IsNumber() ? ToColor(call.Arg(3).AsNumber()) : 0u;
            Framework::Integrations::Client::UI::Nametags::Notes().Set(id, ClampLabel(call.Arg(1).AsString()), duration, color);
        }

        void ClearLabel(Framework::Scripting::Bind::Call &call) {
            if (!call.Arg(0).IsNumber()) {
                call.ThrowError("Nametags.clearLabel: expected (entityId)");
                return;
            }
            Framework::Integrations::Client::UI::Nametags::Notes().Clear(static_cast<uint64_t>(Framework::Scripting::Builtins::ToInt64(call.Arg(0))));
        }

        void ClearLabels(Framework::Scripting::Bind::Call &) {
            Framework::Integrations::Client::UI::Nametags::Notes().ClearAll();
        }
    } // namespace

    void Nametags::Shutdown() {
        Framework::Integrations::Client::UI::Nametags::Notes().ClearAll();
    }

    const Framework::Scripting::Bind::Namespace &Nametags::Definition() {
        static const auto definition = [] {
            const auto number  = Framework::Scripting::Bind::TypeOf<double>();
            const auto boolean = Framework::Scripting::Bind::TypeOf<bool>();
            return Framework::Scripting::Bind::Namespace("Nametags", "The local player's view of the nametags above players: whether they draw at all, whether they carry a health bar, and whether their own is shown.", Framework::Scripting::Bind::Side::Client)
                .Function("setVisible", &SetVisible, Framework::Scripting::Bind::Doc("Shows or hides all nametags for this player only. A player hidden with Player.setNametagVisible stays hidden either way.").Param("visible", boolean, "True to draw nametags, false to hide every one of them."), Framework::Scripting::Bind::Side::Client)
                .Function("isVisible", &IsVisible, Framework::Scripting::Bind::Doc("Checks whether this player draws nametags.").ReturnType(boolean).Returns("True unless they were hidden locally."), Framework::Scripting::Bind::Side::Client)
                .Function("setHealthVisible", &SetHealthVisible, Framework::Scripting::Bind::Doc("Shows or hides the health bar on all nametags for this player only, leaving the names alone.").Param("visible", boolean, "True to draw the health bar under each name, false to hide it."), Framework::Scripting::Bind::Side::Client)
                .Function("isHealthVisible", &IsHealthVisible, Framework::Scripting::Bind::Doc("Checks whether this player draws health bars on nametags.").ReturnType(boolean).Returns("True unless they were hidden locally."), Framework::Scripting::Bind::Side::Client)
                .Function("setSelfVisible", &SetSelfVisible, Framework::Scripting::Bind::Doc("Shows or hides this player's own nametag, drawn as others see it, with any label set on their own id.").Param("visible", boolean, "True to draw this player's own nametag, false (default) to leave it off."),
                    Framework::Scripting::Bind::Side::Client)
                .Function("isSelfVisible", &IsSelfVisible, Framework::Scripting::Bind::Doc("Checks whether this player draws their own nametag.").ReturnType(boolean).Returns("False unless it was turned on locally."), Framework::Scripting::Bind::Side::Client)
                .Function("setLabel", &SetLabel,
                    Framework::Scripting::Bind::Doc("Hangs a transient line on that entity's nametag, above its name -- speech, an emote, a status. It follows the body, fades with distance and hides behind cover exactly as the name does. "
                        "Local to this player: the line is not replicated, and a nametag hidden with Nametags.setVisible draws neither. Player.setNametagText is the server-side counterpart for a lasting name.")
                        .Param("entityId", number, "Network id of the entity to label (server-side `player.id`).")
                        .Param("text", Framework::Scripting::Bind::TypeOf<std::string>(), "Label text; '\\n' splits lines. Empty clears the label.")
                        .OptionalParam("durationMs", number, "How long to hold it (default 6000). <= 0 holds until cleared.")
                        .OptionalParam("color", number, "Packed 0xAARRGGBB; 0 (default) uses the nametag's own colour."),
                    Framework::Scripting::Bind::Side::Client)
                .Function("clearLabel", &ClearLabel, Framework::Scripting::Bind::Doc("Removes an entity's label before its duration elapses.").Param("entityId", number, "Network id of the entity whose label to remove."), Framework::Scripting::Bind::Side::Client)
                .Function("clearLabels", &ClearLabels, Framework::Scripting::Bind::Doc("Removes every label this player is drawing."), Framework::Scripting::Bind::Side::Client);
        }();
        return definition;
    }

    void Nametags::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }
        if (resourceManager != nullptr) {
            Framework::Scripting::Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, target, Framework::Scripting::GetScriptingCatalog(isolate), Framework::Scripting::Bind::Side::Client);
    }

    void Nametags::RegisterLua(Framework::Scripting::LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
