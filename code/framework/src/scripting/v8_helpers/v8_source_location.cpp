/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "v8_source_location.h"

namespace Framework::Scripting::Helpers {
    SourceLocation SourceLocation::GetCurrent(v8::Isolate *isolate) {
        v8::Local<v8::StackTrace> stackTrace = v8::StackTrace::CurrentStackTrace(isolate, 1);
        if (stackTrace->GetFrameCount() > 0) {
            v8::Local<v8::StackFrame> frame = stackTrace->GetFrame(isolate, 0);
            v8::Local<v8::Value> name       = frame->GetScriptName();
            if (!name.IsEmpty()) {
                std::string fileName = *v8::String::Utf8Value(isolate, frame->GetScriptName());
                int line             = frame->GetLineNumber();
                return SourceLocation {std::move(fileName), line};
            }
            else if (frame->IsEval()) {
                return SourceLocation {"[eval]", 0};
            }
        }
        return SourceLocation {"[unknown]", 0};
    }

    SourceLocation::SourceLocation(std::string &&fileName, int lineNumber): _fileName(fileName), _lineNumber(lineNumber) {}
} // namespace Framework::Scripting::Helpers
