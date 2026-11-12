/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <external/imgui/font_family.h>

#include <v8.h>

#include <functional>
#include <string>

namespace Framework::Scripting {
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Integrations::Client::Scripting::Builtins {

    // The global Fonts: a client resource names a font family after TTF/OTF files it ships, and
    // world text that asks for the family by name (a TextLabel's font) draws in it. Families last
    // for the session; the files are read from the calling resource's own package.
    class Fonts final {
      public:
        static constexpr size_t kMaxFileBytes = 32u << 20;

        using AddFamily = std::function<void(const std::string &family, External::ImGUI::FontFamilySource faces)>;
        // Where families go: the client instance points it at its ImGui atlas. With none, register
        // still checks its files, then logs and drops them.
        static void SetTarget(AddFamily add, std::function<void()> clear);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

        // The session is over: its families' names mean nothing on the next server.
        static void Reset();
        // The resource manager is going away.
        static void Shutdown();

      private:
        static void RegisterCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        inline static Framework::Scripting::ResourceManager *_resourceManager = nullptr;
        inline static AddFamily _add;
        inline static std::function<void()> _clear;
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
