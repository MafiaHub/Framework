/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/builtins/property.h"
#include "scripting/node_engine.h"

namespace {
    class PropertyBindingFixture {
      public:
        int GetValue() const {
            return value_;
        }

        void SetValue(int value) {
            value_ = value;
        }

      private:
        int value_ = 7;
    };
} // namespace

MODULE(property_bindings, {
    using namespace Framework::Scripting;
    using namespace Framework::Scripting::Builtins;

    IT("Documented property helpers bind runtime accessors and metadata together", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        v8pp::metadata::registry metadata;
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8pp::class_<PropertyBindingFixture> fixture(isolate, metadata, "PropertyBindingFixture");
            fixture.ctor<>();
            RegisterProperty<PropertyBindingFixture, &PropertyBindingFixture::GetValue, &PropertyBindingFixture::SetValue>(fixture, "value", v8pp::metadata::property_docs("number", "Writable value"));
            RegisterReadonlyProperty<PropertyBindingFixture, &PropertyBindingFixture::GetValue>(fixture, "readonlyValue", v8pp::metadata::property_docs("number", "Read-only value"));
            fixture.publish(context->Global());
        }

        const auto writableResult = RunJS(engine, "(() => { const fixture = new PropertyBindingFixture(); "
                                                  "fixture.value = 42; return fixture.value; })()");
        const auto readonlyResult = RunJS(engine, "(() => { const fixture = new PropertyBindingFixture(); "
                                                  "try { fixture.readonlyValue = 42; } catch {} return fixture.readonlyValue; })()");

        const auto &symbol = metadata.symbols().front();
        EQUALS(writableResult, 42);
        EQUALS(readonlyResult, 7);
        EQUALS(symbol.properties.size(), static_cast<size_t>(2));
        STREQUALS(symbol.properties[0].name.c_str(), "value");
        EQUALS(symbol.properties[0].readonly, false);
        STREQUALS(symbol.properties[0].value_type.name.c_str(), "number");
        STREQUALS(symbol.properties[1].name.c_str(), "readonlyValue");
        EQUALS(symbol.properties[1].readonly, true);

        v8pp::class_<PropertyBindingFixture>::destroy(engine.GetIsolate());
        engine.Shutdown();
    });
})
