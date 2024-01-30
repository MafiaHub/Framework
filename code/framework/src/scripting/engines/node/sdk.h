/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

#include "../callback.h"
#include "engine.h"

namespace Framework::Scripting::Engines::Node {
    class SDK {
      private:
        v8pp::module *_module = nullptr;
        v8::Isolate *_isolate = nullptr;

      public:
        bool Init(Engine *, v8::Isolate *, SDKRegisterCallback = nullptr);

        v8pp::module *GetModule() const {
            return _module;
        }

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        v8::Local<v8::ObjectTemplate> GetObjectTemplate() const {
            return _module->impl();
        }

        v8::Local<v8::Object> GetNewInstance() const {
            return _module->new_instance();
        }
    };
} // namespace Framework::Scripting::Engines::Node
