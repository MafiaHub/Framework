/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

namespace Framework::Scripting::Engines::Node {
    class SDK {
      private:
        v8pp::module *_module;

      public:
        bool Init(v8::Isolate *);

        v8::Local<v8::Object> GetNewInstance() const {
            return _module->new_instance();
        }
    };
} // namespace Framework::Scripting::Engines::Node