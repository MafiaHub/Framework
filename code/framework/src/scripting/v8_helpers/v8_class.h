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

#include <functional>
#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    using ClassInitCallback = std::function<void(v8::Local<v8::FunctionTemplate>)>;
    class V8Class {
      public:
        std::string _name;
        bool _loaded;
        ClassInitCallback _initCb;
        v8::FunctionCallback _constructor;
        v8::Persistent<v8::FunctionTemplate> _fnTpl;
        V8Class *_parent;

      public:
        V8Class(const std::string &, ClassInitCallback && = {});
        V8Class(const std::string &, v8::FunctionCallback, ClassInitCallback && = {});
        V8Class(const std::string &, V8Class &, ClassInitCallback && = {});
        V8Class(const std::string &, V8Class &, v8::FunctionCallback, ClassInitCallback && = {});

        V8HelperError Load();
        V8HelperError Register(v8::Local<v8::Context>, v8::Local<v8::Object>);

        v8::Local<v8::Object> CreateInstance(v8::Local<v8::Context>);
        v8::Local<v8::Value> CreateInstance(v8::Isolate *, v8::Local<v8::Context>, std::vector<v8::Local<v8::Value>>);

        std::string GetName() const {
            return _name;
        }
    };
} // namespace Framework::Scripting::Helpers
