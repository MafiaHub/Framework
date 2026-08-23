/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "nametags.h"

#include <external/imgui/widgets/nametag.h>
#include <scripting/scripting_catalog.h>

#include <v8pp/convert.hpp>

#include <string>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
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
        target->Set(context, v8pp::to_v8(isolate, "Nametags"), nametagsObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Nametags", "The local player's view of the nametags above other players: whether they draw at all, and whether they carry a health bar.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setVisible",
            v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to draw nametags, false to hide every one of them.")},
                "Shows or hides all nametags for this player only. A player hidden with Player.setNametagVisible stays hidden either way.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isVisible", v8pp::metadata::docs("boolean", {}, "Checks whether this player draws nametags.", "True unless they were hidden locally.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("setHealthVisible",
            v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "True to draw the health bar under each name, false to hide it.")},
                "Shows or hides the health bar on all nametags for this player only, leaving the names alone.")));
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("isHealthVisible", v8pp::metadata::docs("boolean", {}, "Checks whether this player draws health bars on nametags.", "True unless they were hidden locally.")));
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
