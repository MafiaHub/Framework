/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "init.h"
#include "resource.h"

#include <functional>
#include <map>
#include <string>
#include <v8.h>

namespace Framework::Scripting {
    using InvokeEventCallback = std::function<std::vector<v8::Local<v8::Value>>(v8::Isolate *, v8::Local<v8::Context>)>;

    class Engine;
    class ResourceManager {
      private:
        Engine *_engine = nullptr;
        std::map<std::string, Resource *> _resources;

      public:
        ResourceManager(Engine *);

        ~ResourceManager();

        ResourceManagerError Load(std::string, SDKRegisterCallback = nullptr);

        void LoadAll(SDKRegisterCallback = nullptr);

        ResourceManagerError Unload(std::string);

        void UnloadAll();

        size_t ResourcesLength() const {
            return _resources.size();
        }

        std::map<std::string, Resource *> GetAllResources() const {
            return _resources;
        }

        void InvokeErrorEvent(const std::string &, const std::string &, const std::string &, int32_t);

        void InvokeEvent(const std::string &, InvokeEventCallback cb);
    };
} // namespace Framework::Scripting
