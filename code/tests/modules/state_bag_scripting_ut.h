/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/shared/scripting/state_bag_events.h"
#include "networking/replication/state_bag.h"
#include "scripting/builtins/state_bag.h"
#include "scripting/node_engine.h"

#include <v8.h>
#include <v8pp/convert.hpp>

#include <string>

// The scripting half of state bags: the value conversions a script's writes and reads go through,
// and the argument shape an entityStateChange handler is given.
//
// Not covered: the emit itself. InstallStateBagEvents needs a replication manager, a scripting
// module registered in CoreModules and a resource with a handler in it before anything is
// observable, and CoreModules refuses a second registration -- so a test of it would be asserting
// against a rig rather than against the code. The two decisions inside it that could be wrong on
// their own are pulled out and tested here instead: StateChangeArgs, and the guard that returns an
// invalid handle when there is no replication.
MODULE(state_bag_scripting, {
    using Framework::Networking::Replication::StateChange;
    using Framework::Networking::Replication::StateValue;
    using Bag = Framework::Scripting::Builtins::StateBag;
    namespace Events = Framework::Integrations::Shared::Scripting;

    IT("returns no handle when there is no replication to subscribe to", {
        // CoreModules is unset in this binary, which is the same state an instance is in before its
        // networking engine comes up.
        EQUALS(Events::InstallStateBagEvents([](v8::Isolate *, uint64_t) {
                   return v8::Local<v8::Value>();
               }) == Framework::Networking::Replication::kInvalidStateChangeHandle,
            true);
    });

    IT("releasing an unset subscription does nothing", {
        Framework::Networking::Replication::StateChangeHandle handle = Framework::Networking::Replication::kInvalidStateChangeHandle;
        Events::ReleaseStateBagEvents(handle);
        EQUALS(handle == Framework::Networking::Replication::kInvalidStateChangeHandle, true);
    });

    // One engine for the cases that need an isolate; torn down at the end of the module. EQUALS
    // expands to a break, so it is only legal inside an IT -- the init result is asserted in one.
    Framework::Scripting::NodeEngine engine({});
    const bool engineReady = engine.Init() == Framework::Scripting::ScriptingError::SCRIPTING_NONE;

    IT("brings up an isolate for the conversion cases", {
        EQUALS(engineReady, true);
    });

    if (engineReady) {
        v8::Isolate *isolate = engine.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine.GetContext();
        v8::Context::Scope contextScope(context);

        IT("converts a boolean", {
            StateValue out;
            EQUALS(Bag::ToStateValue(isolate, context, v8::Boolean::New(isolate, true), out), true);
            EQUALS(out.type == StateValue::Type::Boolean, true);
            EQUALS(out.boolean, true);
        });

        IT("converts a number", {
            StateValue out;
            EQUALS(Bag::ToStateValue(isolate, context, v8::Number::New(isolate, 42.5), out), true);
            EQUALS(out.type == StateValue::Type::Number, true);
            EQUALS(out.number == 42.5, true);
        });

        IT("converts a string", {
            StateValue out;
            EQUALS(Bag::ToStateValue(isolate, context, v8pp::to_v8(isolate, "blacksmith"), out), true);
            EQUALS(out.type == StateValue::Type::String, true);
            STREQUALS(out.text.c_str(), "blacksmith");
        });

        IT("converts null and undefined to the same null value", {
            StateValue fromNull;
            StateValue fromUndefined;
            EQUALS(Bag::ToStateValue(isolate, context, v8::Null(isolate), fromNull), true);
            EQUALS(Bag::ToStateValue(isolate, context, v8::Undefined(isolate), fromUndefined), true);
            // A script storing either means "no value"; only an absent key is distinguishable, and
            // that is carried by the bag rather than by the value.
            EQUALS(fromNull.type == StateValue::Type::Null, true);
            EQUALS(fromUndefined.type == StateValue::Type::Null, true);
        });

        IT("serializes anything structured as JSON", {
            v8::Local<v8::Object> object = v8::Object::New(isolate);
            object->Set(context, v8pp::to_v8(isolate, "a"), v8::Number::New(isolate, 1)).Check();

            StateValue out;
            EQUALS(Bag::ToStateValue(isolate, context, object, out), true);
            EQUALS(out.type == StateValue::Type::Json, true);
            STREQUALS(out.text.c_str(), R"({"a":1})");
        });

        IT("refuses a value JSON cannot express", {
            // A cycle: Stringify throws, and the conversion has to report that rather than store
            // something and leave the exception pending for an unrelated call to trip over.
            v8::Local<v8::Object> object = v8::Object::New(isolate);
            object->Set(context, v8pp::to_v8(isolate, "self"), object).Check();

            v8::TryCatch tryCatch(isolate);
            StateValue out;
            EQUALS(Bag::ToStateValue(isolate, context, object, out), false);
            EQUALS(tryCatch.HasCaught(), true);
            tryCatch.Reset();
        });

        IT("reads each value type back", {
            StateValue boolean;
            boolean.type    = StateValue::Type::Boolean;
            boolean.boolean = true;
            EQUALS(Bag::FromStateValue(isolate, context, boolean)->IsBoolean(), true);

            StateValue number;
            number.type   = StateValue::Type::Number;
            number.number = 42.5;
            EQUALS(Bag::FromStateValue(isolate, context, number)->IsNumber(), true);

            StateValue text;
            text.type = StateValue::Type::String;
            text.text = "blacksmith";
            EQUALS(Bag::FromStateValue(isolate, context, text)->IsString(), true);

            EQUALS(Bag::FromStateValue(isolate, context, StateValue {})->IsNull(), true);
        });

        IT("parses a JSON value back into an object", {
            StateValue document;
            document.type = StateValue::Type::Json;
            document.text = R"({"a":1})";

            v8::Local<v8::Value> value = Bag::FromStateValue(isolate, context, document);
            EQUALS(value->IsObject(), true);

            v8::Local<v8::Value> a;
            EQUALS(value.As<v8::Object>()->Get(context, v8pp::to_v8(isolate, "a")).ToLocal(&a), true);
            EQUALS(a->IsNumber(), true);
        });

        IT("hands back raw text for a JSON value that no longer parses", {
            StateValue broken;
            broken.type = StateValue::Type::Json;
            broken.text = "{not json";

            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::Value> value = Bag::FromStateValue(isolate, context, broken);
            // A malformed value means a peer sent something broken. Reading it must not be what
            // breaks the script, and must not leave an exception pending for the next call.
            EQUALS(value->IsString(), true);
            EQUALS(tryCatch.HasCaught(), false);
        });

        IT("builds the four arguments a change handler is given", {
            StateChange change;
            change.key         = "job";
            change.value.type  = StateValue::Type::String;
            change.value.text  = "blacksmith";
            change.hadPrevious = true;
            change.previous    = change.value;
            change.previous.text = "farmer";

            const auto args = Events::StateChangeArgs(isolate, context, change, v8pp::to_v8(isolate, "entity-handle"));
            EQUALS(args.size(), size_t(4));
            // The handle is passed through untouched: deciding it is the game's job, not this one's.
            EQUALS(args[0]->IsString(), true);
            EQUALS(args[1]->IsString(), true);
            EQUALS(args[2]->IsString(), true);
            EQUALS(args[3]->IsString(), true);
        });

        IT("reports a removed key as undefined rather than null", {
            StateChange change;
            change.key         = "job";
            change.removed     = true;
            change.hadPrevious = true;
            change.previous.type = StateValue::Type::String;
            change.previous.text = "blacksmith";

            const auto args = Events::StateChangeArgs(isolate, context, change, v8::Null(isolate));
            // null is a value a script can store, so a removal cannot report it and stay
            // distinguishable from a key that holds null.
            EQUALS(args[2]->IsUndefined(), true);
            EQUALS(args[3]->IsString(), true);
        });

        IT("reports a key that held nothing before as undefined", {
            StateChange change;
            change.key        = "job";
            change.value.type = StateValue::Type::String;
            change.value.text = "blacksmith";

            const auto args = Events::StateChangeArgs(isolate, context, change, v8::Null(isolate));
            EQUALS(args[2]->IsString(), true);
            EQUALS(args[3]->IsUndefined(), true);
        });

        IT("reports a stored null as null, not as undefined", {
            StateChange change;
            change.key         = "job";
            change.hadPrevious = true;
            // Both the new and the previous value are a stored null. Neither is an absence, and the
            // whole reason removals use undefined is so this case stays readable.
            const auto args = Events::StateChangeArgs(isolate, context, change, v8::Null(isolate));
            EQUALS(args[2]->IsNull(), true);
            EQUALS(args[3]->IsNull(), true);
        });
    }

    engine.Shutdown();
})
