/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "v8_try_catch.h"

#include <logging/logger.h>

namespace Framework::Scripting::Helpers {
    bool TryCatch(const fu2::function<bool() const> &fn, v8::Isolate *isolate, v8::Local<v8::Context> context) {
        if (!isolate) {
            isolate = v8::Isolate::GetCurrent();
        }

        if (!isolate) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] Failed to acquire isolate instance");
            return false;
        }

        if (context.IsEmpty()) {
            context = isolate->GetEnteredOrMicrotaskContext();
        }

        if (context.IsEmpty()) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] Failed to acquire context instance");
            return false;
        }

        v8::TryCatch tryCatch(isolate);

        if (!fn()) {
            v8::Local<v8::Value> exception = tryCatch.Exception();
            v8::Local<v8::Message> message = tryCatch.Message();

            if (!message.IsEmpty() && !context.IsEmpty()) {
                v8::MaybeLocal<v8::String> maybeSourceLine = message->GetSourceLine(context);
                v8::Maybe<int32_t> line                    = message->GetLineNumber(context);
                v8::ScriptOrigin origin                    = message->GetScriptOrigin();

                if (!origin.ResourceName()->IsUndefined()) {
                    Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] exception at {}: {}", *v8::String::Utf8Value(isolate, origin.ResourceName()), line.ToChecked());

                    if (!maybeSourceLine.IsEmpty()) {
                        v8::Local<v8::String> sourceLine = maybeSourceLine.ToLocalChecked();

                        if (sourceLine->Length() <= 80) {
                            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] {}", *v8::String::Utf8Value(isolate, sourceLine));
                        }
                        else {
                            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] {}", std::string {*v8::String::Utf8Value(isolate, sourceLine), 80});
                        }
                    }

                    auto stackTrace = tryCatch.StackTrace(context);
                    /*resource->InvokeErrorEvent(exception.IsEmpty() ? "unknown" : *v8::String::Utf8Value(isolate,
                       exception),
                        (!stackTrace.IsEmpty() && stackTrace.ToLocalChecked()->IsString()) ?
                       *v8::String::Utf8Value(isolate, stackTrace.ToLocalChecked()) : "",
                        *v8::String::Utf8Value(isolate, origin.ResourceName()), line.ToChecked());*/
                }
                else {
                    Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] Exception");
                }

                v8::MaybeLocal<v8::Value> stackTrace = tryCatch.StackTrace(context);
                if (!stackTrace.IsEmpty() && stackTrace.ToLocalChecked()->IsString()) {
                    v8::String::Utf8Value stackTraceStr(isolate, stackTrace.ToLocalChecked().As<v8::String>());
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] {}", *stackTraceStr);
                }
            }
            else if (!exception.IsEmpty()) {
                Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] Exception: {}", *v8::String::Utf8Value(isolate, exception));
            }
            else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] Exception occured");
            }

            return false;
        }

        return true;
    }
} // namespace Framework::Scripting::Helpers
