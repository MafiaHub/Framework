/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// This file and its CPP code were implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include "../errors.h"
#include "v8_class.h"

#include <map>
#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    using ModuleInitCallback = std::function<void(v8::Local<v8::Context>, v8::Local<v8::Object>)>;
    class V8Module {
      private:
        std::string _name;
        ModuleInitCallback _initCb;

        v8::Local<v8::ObjectTemplate> _objTemplate;

        std::map<std::string, V8Class *> _classes;

      public:
        V8Module(std::string, ModuleInitCallback);

        V8HelperError AddClass(V8Class *);

        V8Class *GetClass(const std::string &);

        V8HelperError Register(v8::Local<v8::Context>);
    };
} // namespace Framework::Scripting::Helpers
