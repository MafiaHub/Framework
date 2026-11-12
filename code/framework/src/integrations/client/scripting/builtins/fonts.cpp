/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "fonts.h"

#include <logging/logger.h>
#include <networking/replication/entities/text_label_entity.h>
#include <scripting/engine_helpers.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>
#include <utils/vfs.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace Framework::Integrations::Client::Scripting::Builtins {
    namespace {
        constexpr std::string_view kFaces[] {"regular", "bold", "italic", "boldItalic"};

        bool Fail(v8::Isolate *isolate, const std::string &message) {
            Framework::Scripting::ThrowError(isolate, ("Fonts.register: " + message).c_str());
            return false;
        }

        bool FailType(v8::Isolate *isolate, const std::string &message) {
            Framework::Scripting::ThrowTypeError(isolate, ("Fonts.register: " + message).c_str());
            return false;
        }

        // From the resource's own package only: never the disk, nor another resource's files.
        bool ReadFace(v8::Isolate *isolate, const std::string &resource, std::string_view face, const std::string &relative, std::string &out) {
            const std::string root  = Framework::Utils::Vfs::ResourcePath(resource) + "/";
            const std::string path  = Framework::Utils::Vfs::NormalizeVirtual(root + relative);
            const std::string label = std::string(face) + " '" + relative + "'";
            if (path.size() <= root.size() || path.compare(0, root.size(), root) != 0) {
                return Fail(isolate, label + " is not a file inside resource '" + resource + "'");
            }
            auto &vfs       = Framework::Utils::Vfs::Get();
            const auto size = vfs.Size(path);
            if (!size) {
                return Fail(isolate, label + " is not in resource '" + resource + "'");
            }
            if (*size > Fonts::kMaxFileBytes) {
                return Fail(isolate, label + " is larger than " + std::to_string(Fonts::kMaxFileBytes >> 20) + " MiB");
            }
            if (!vfs.Read(path, out)) {
                return Fail(isolate, label + " could not be read");
            }
            if (Framework::External::ImGUI::IsWebFont(out)) {
                return Fail(isolate, label + " is WOFF/WOFF2, which cannot be loaded; convert it to TTF or OTF");
            }
            if (!Framework::External::ImGUI::IsFontFile(out)) {
                return Fail(isolate, label + " is not a TrueType or OpenType font");
            }
            return true;
        }
    } // namespace

    void Fonts::SetTarget(AddFamily add, std::function<void()> clear) {
        _add   = std::move(add);
        _clear = std::move(clear);
    }

    void Fonts::RegisterCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope hs(isolate);
        auto context = isolate->GetCurrentContext();

        constexpr size_t kMaxFamilyBytes = Framework::Networking::Replication::Entities::kTextLabelMaxFontBytes;
        const std::string family         = args.Length() >= 1 && args[0]->IsString() ? v8pp::from_v8<std::string>(isolate, args[0]) : std::string();
        if (family.empty() || family.size() > kMaxFamilyBytes) {
            FailType(isolate, "family must be a name of 1 to " + std::to_string(kMaxFamilyBytes) + " bytes");
            return;
        }

        // Paths per face, in kFaces order; a string alone is the regular face.
        std::array<std::string, std::size(kFaces)> paths;
        if (args.Length() >= 2 && args[1]->IsString()) {
            paths[0] = v8pp::from_v8<std::string>(isolate, args[1]);
        }
        else if (args.Length() >= 2 && args[1]->IsObject() && !args[1]->IsArray()) {
            const auto faces = args[1].As<v8::Object>();
            v8::Local<v8::Array> keys;
            if (!faces->GetOwnPropertyNames(context).ToLocal(&keys)) {
                return;
            }
            for (uint32_t i = 0; i < keys->Length(); ++i) {
                v8::Local<v8::Value> key;
                if (!keys->Get(context, i).ToLocal(&key)) {
                    return;
                }
                const auto name = v8pp::from_v8<std::string>(isolate, key);
                if (std::find(std::begin(kFaces), std::end(kFaces), name) == std::end(kFaces)) {
                    FailType(isolate, "unknown face '" + name + "'; faces are regular, bold, italic and boldItalic");
                    return;
                }
            }
            for (size_t i = 0; i < std::size(kFaces); ++i) {
                v8::Local<v8::Value> value;
                if (!faces->Get(context, v8pp::to_v8(isolate, kFaces[i])).ToLocal(&value)) {
                    return;
                }
                if (value->IsUndefined()) {
                    continue;
                }
                if (!value->IsString()) {
                    FailType(isolate, std::string(kFaces[i]) + " must be a path inside the resource");
                    return;
                }
                paths[i] = v8pp::from_v8<std::string>(isolate, value);
            }
        }
        else {
            FailType(isolate, "expected (family, path | { regular, bold?, italic?, boldItalic? })");
            return;
        }
        if (paths[0].empty()) {
            FailType(isolate, "the regular face is required");
            return;
        }

        const std::string resource = _resourceManager ? _resourceManager->ResolveResourceContext(isolate, {}) : std::string();
        if (resource.empty()) {
            Fail(isolate, "must be called from a resource's script");
            return;
        }

        // Every face is read before any is registered, so a bad one leaves the family as it was.
        std::array<std::string, std::size(kFaces)> bytes;
        for (size_t i = 0; i < std::size(kFaces); ++i) {
            if (!paths[i].empty() && !ReadFace(isolate, resource, kFaces[i], paths[i], bytes[i])) {
                return;
            }
        }
        // Without ImGui no label draws at all, so a throw would only break the resource's script.
        if (!_add) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Fonts.register: '{}' not registered: this client has no ImGui renderer", family);
            return;
        }
        _add(family, {std::move(bytes[0]), std::move(bytes[1]), std::move(bytes[2]), std::move(bytes[3])});
    }

    void Fonts::Reset() {
        if (_clear) {
            _clear();
        }
    }

    void Fonts::Shutdown() {
        _resourceManager = nullptr;
    }

    void Fonts::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager) {
        if (!isolate || context.IsEmpty() || target.IsEmpty()) {
            return;
        }
        _resourceManager = resourceManager;

        v8::Local<v8::Object> fontsObj = v8::Object::New(isolate);
        fontsObj->Set(context, v8pp::to_v8(isolate, "register"), v8::FunctionTemplate::New(isolate, &Fonts::RegisterCallback)->GetFunction(context).ToLocalChecked()).Check();
        target->Set(context, v8pp::to_v8(isolate, "Fonts"), fontsObj).Check();

        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Fonts", "Font families this client draws world text in, from TTF or OTF files a client resource ships.");
        metadata.record(v8pp::metadata::function_of<v8::FunctionCallback>("register",
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("family", "string", false, "The name labels ask for (a TextLabel's font), at most 64 bytes."),
                    v8pp::metadata::param("faces", "string | { regular: string; bold?: string; italic?: string; boldItalic?: string }", false,
                        "Paths inside this resource to TTF or OTF files, at most 32 MiB each; one path is the regular face. {b} and {i} markup synthesize a face that is left out.")},
                "Names a font family after files this resource ships. It draws from the next frame on, for the rest of the session; registering a family again replaces its faces. Characters a face lacks draw in the game's font. "
                "Throws, changing nothing, for a file that is missing, outside the resource, WOFF/WOFF2 or no font at all. A file that passes those checks but fails to load is logged and draws in the game's font.")));
    }

} // namespace Framework::Integrations::Client::Scripting::Builtins
