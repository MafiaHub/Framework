/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "v8_class.h"

#include "v8_try_catch.h"

namespace Framework::Scripting::Helpers {
    V8Class::V8Class(const std::string &name, ClassInitCallback &&cb): _name(name), _parent(nullptr), _constructor(nullptr), _loaded(false), _initCb(cb) {}

    V8Class::V8Class(const std::string &name, v8::FunctionCallback constructor, ClassInitCallback &&cb)
        : _name(name)
        , _parent(nullptr)
        , _constructor(constructor)
        , _loaded(false)
        , _initCb(cb) {}

    V8Class::V8Class(const std::string &name, V8Class &parent, ClassInitCallback &&cb): _name(name), _parent(&parent), _constructor(nullptr), _loaded(false), _initCb(cb) {}

    V8Class::V8Class(const std::string &name, V8Class &parent, v8::FunctionCallback constructor, ClassInitCallback &&cb)
        : _name(name)
        , _parent(&parent)
        , _constructor(constructor)
        , _loaded(false)
        , _initCb(cb) {}

    V8HelperError V8Class::Load() {
        auto isolate                        = v8::Isolate::GetCurrent();
        v8::Local<v8::FunctionTemplate> tpl = v8::FunctionTemplate::New(isolate, _constructor);
        tpl->SetClassName(v8::String::NewFromUtf8(isolate, _name.c_str(), v8::NewStringType::kNormal).ToLocalChecked());

        if (_initCb) {
            _initCb(tpl);
        }

        if (_parent) {
            if (!_parent->_loaded)
                _parent->Load();
            auto parentTpl = _parent->_fnTpl.Get(isolate);
            tpl->Inherit(parentTpl);

            // Make sure the template has the the needed amount of internal fields for the parent
            auto parentInternalFieldCount = parentTpl->InstanceTemplate()->InternalFieldCount();
            if (parentInternalFieldCount > tpl->InstanceTemplate()->InternalFieldCount())
                tpl->InstanceTemplate()->SetInternalFieldCount(parentInternalFieldCount);
        }

        _fnTpl.Reset(isolate, tpl);
        _loaded = true;
        return Framework::Scripting::V8HelperError::HELPER_NONE;
    }

    V8HelperError V8Class::Register(v8::Local<v8::Context> context, v8::Local<v8::Object> obj) {
        auto isolate = v8::Isolate::GetCurrent();
        if (!isolate) {
            return Framework::Scripting::V8HelperError::HELPER_ISOLATE_NULL;
        }

        if (context.IsEmpty()) {
            return Framework::Scripting::V8HelperError::HELPER_CONTEXT_EMPTY;
        }

        v8::Maybe<bool> res = obj->Set(
            context, v8::String::NewFromUtf8(isolate, _name.c_str(), v8::NewStringType::kNormal).ToLocalChecked(), _fnTpl.Get(isolate)->GetFunction(context).ToLocalChecked());

        // If something went weird, just cancel and evacuate
        if (!res.ToChecked()) {
            return Framework::Scripting::V8HelperError::HELPER_REGISTER_FAILED;
        }

        return Framework::Scripting::V8HelperError::HELPER_NONE;
    }

    v8::Local<v8::Object> V8Class::CreateInstance(v8::Local<v8::Context> context) {
        auto isolate              = v8::Isolate::GetCurrent();
        v8::Local<v8::Object> obj = _fnTpl.Get(isolate)->InstanceTemplate()->NewInstance(context).ToLocalChecked();
        return obj;
    }

    v8::Local<v8::Value> V8Class::CreateInstance(v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<v8::Local<v8::Value>> args) {
        v8::Local<v8::Function> constructor = _fnTpl.Get(isolate)->GetFunction(context).ToLocalChecked();
        v8::Local<v8::Value> obj;

        TryCatch([&] {
            v8::MaybeLocal<v8::Value> maybeObj = constructor->CallAsConstructor(context, args.size(), args.data());

            if (maybeObj.IsEmpty())
                return false;

            obj = maybeObj.ToLocalChecked();
            return true;
        });
        return obj;
    }
} // namespace Framework::Scripting::Helpers
