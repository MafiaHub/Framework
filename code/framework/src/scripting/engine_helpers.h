#pragma once

#include <cerrno>

#include <v8.h>

#include <cstring>
#include <string>

namespace Framework::Scripting {

    // Formats a V8 exception with stack trace for better error messages
    inline std::string FormatV8Exception(v8::Isolate *isolate, v8::TryCatch &tryCatch, const char *fallbackMessage) {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        // Try to get the stack trace first (most informative)
        v8::Local<v8::Value> stackTrace;
        if (tryCatch.StackTrace(context).ToLocal(&stackTrace) && stackTrace->IsString()) {
            v8::String::Utf8Value stackStr(isolate, stackTrace);
            if (*stackStr && strlen(*stackStr) > 0) {
                return *stackStr;
            }
        }

        // Fall back to exception message with manual location info
        v8::Local<v8::Value> exception = tryCatch.Exception();
        v8::String::Utf8Value exceptionStr(isolate, exception);
        std::string result = (*exceptionStr && strlen(*exceptionStr) > 0) ? *exceptionStr : fallbackMessage;

        // Try to add location information from the message
        v8::Local<v8::Message> message = tryCatch.Message();
        if (!message.IsEmpty()) {
            v8::Local<v8::Value> resourceName = message->GetScriptResourceName();
            if (!resourceName.IsEmpty() && !resourceName->IsUndefined()) {
                v8::String::Utf8Value resourceNameStr(isolate, resourceName);
                if (*resourceNameStr) {
                    int lineNumber = message->GetLineNumber(context).FromMaybe(-1);
                    int startColumn = message->GetStartColumn(context).FromMaybe(-1);

                    result += "\n    at ";
                    result += *resourceNameStr;
                    if (lineNumber >= 0) {
                        result += ":" + std::to_string(lineNumber);
                        if (startColumn >= 0) {
                            result += ":" + std::to_string(startColumn + 1);
                        }
                    }
                }
            }
        }

        return result;
    }

} // namespace Framework::Scripting
