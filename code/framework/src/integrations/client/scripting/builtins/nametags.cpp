/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "nametags.h"

#include <external/imgui/widgets/nametag.h>
#include <integrations/client/ui/nametag_notes.h>
#include <scripting/scripting_catalog.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

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
    } // namespace

    void Nametags::SetVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1) {
            ThrowError(isolate, "Nametags.setVisible: expected (visible)");
            return;
        }
        Framework::External::ImGUI::Widgets::NameTagView::showTags = args[0]->BooleanValue(isolate);
    }

    void Nametags::IsVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(Framework::External::ImGUI::Widgets::NameTagView::showTags);
    }

    void Nametags::SetHealthVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1) {
            ThrowError(isolate, "Nametags.setHealthVisible: expected (visible)");
            return;
        }
        Framework::External::ImGUI::Widgets::NameTagView::showHealth = args[0]->BooleanValue(isolate);
    }

    void Nametags::IsHealthVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(Framework::External::ImGUI::Widgets::NameTagView::showHealth);
    }

    void Nametags::SetLabelCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsString()) {
            ThrowError(isolate, "Nametags.setLabel: expected (entityId, text, durationMs?, color?)");
            return;
        }
        const auto id       = static_cast<uint64_t>(args[0]->IntegerValue(context).FromMaybe(0));
        const auto duration = args.Length() > 2 && args[2]->IsNumber() ? static_cast<float>(args[2]->NumberValue(context).FromMaybe(0.0)) : 6000.0f;
        const auto color    = args.Length() > 3 && args[3]->IsNumber() ? static_cast<uint32_t>(args[3]->NumberValue(context).FromMaybe(0.0)) : 0u;
        Framework::Integrations::Client::UI::Nametags::Notes().Set(id, ClampLabel(v8pp::from_v8<std::string>(isolate, args[1])), duration, color);
    }

    void Nametags::ClearLabelCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        if (args.Length() < 1 || !args[0]->IsNumber()) {
            ThrowError(isolate, "Nametags.clearLabel: expected (entityId)");
            return;
        }
        Framework::Integrations::Client::UI::Nametags::Notes().Clear(static_cast<uint64_t>(args[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0)));
    }

    void Nametags::ClearLabelsCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        (void)args;
        Framework::Integrations::Client::UI::Nametags::Notes().ClearAll();
    }

    void Nametags::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        (void)resourceManager;
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }

        const auto attach = [&](v8::Local<v8::Object> obj, const char *name, v8::FunctionCallback callback) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback);
            obj->Set(context, v8pp::to_v8(isolate, name), tmpl->GetFunction(context).ToLocalChecked()).Check();
        };

        v8::Local<v8::Object> nametagsObj = v8::Object::New(isolate);
        attach(nametagsObj, "setVisible", &Nametags::SetVisibleCallback);
        attach(nametagsObj, "isVisible", &Nametags::IsVisibleCallback);
        attach(nametagsObj, "setHealthVisible", &Nametags::SetHealthVisibleCallback);
        attach(nametagsObj, "isHealthVisible", &Nametags::IsHealthVisibleCallback);
        attach(nametagsObj, "setLabel", &Nametags::SetLabelCallback);
        attach(nametagsObj, "clearLabel", &Nametags::ClearLabelCallback);
        attach(nametagsObj, "clearLabels", &Nametags::ClearLabelsCallback);
        target->Set(context, v8pp::to_v8(isolate, "Nametags"), nametagsObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Nametags", "The local player's view of the nametags above other players: whether they draw at all, and whether they carry a health bar.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setVisible",
            v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to draw nametags, false to hide every one of them.")}, "Shows or hides all nametags for this player only. A player hidden with Player.setNametagVisible stays hidden either way.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isVisible", v8pp::metadata::docs("boolean", {}, "Checks whether this player draws nametags.", "True unless they were hidden locally.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setHealthVisible",
            v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to draw the health bar under each name, false to hide it.")}, "Shows or hides the health bar on all nametags for this player only, leaving the names alone.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isHealthVisible", v8pp::metadata::docs("boolean", {}, "Checks whether this player draws health bars on nametags.", "True unless they were hidden locally.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setLabel",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("entityId", "number", false, "Network id of the entity to label (server-side `player.id`)."), v8pp::metadata::param("text", "string", false, "Label text; '\\n' splits lines. Empty clears the label."),
                    v8pp::metadata::param("durationMs", "number", true, "How long to hold it (default 6000). <= 0 holds until cleared."), v8pp::metadata::param("color", "number", true, "Packed 0xAARRGGBB; 0 (default) uses the nametag's own colour.")},
                "Hangs a transient line on that entity's nametag, above its name -- speech, an emote, a status. It follows the body, fades with distance and hides behind cover exactly as the name does. "
                "Local to this player: the line is not replicated, and a nametag hidden with Nametags.setVisible draws neither. Player.setNametagText is the server-side counterpart for a lasting name.")));
        metadata.record(
            v8pp::metadata::function_of<v8::FunctionCallback>("clearLabel", v8pp::metadata::docs("void", {v8pp::metadata::param("entityId", "number", false, "Network id of the entity whose label to remove.")}, "Removes an entity's label before its duration elapses.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("clearLabels", v8pp::metadata::docs("void", {}, "Removes every label this player is drawing.")));
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
