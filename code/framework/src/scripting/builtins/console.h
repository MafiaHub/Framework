/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../v8_helpers/v8_string.h"

#include <logging/logger.h>
#include <src/debug/debug-interface.h>
#include <src/debug/interface-types.h>
#include <sstream>
#include <v8.h>

namespace Framework::Scripting::Builtins {
    class ConsoleDelegate: public v8::debug::ConsoleDelegate {
      private:
        std::string Format(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            std::stringstream out;

            auto isolate        = v8::Isolate::GetCurrent();
            auto currentContext = isolate->GetCurrentContext();

            for (int i = 0; i < args.Length(); i++) {
                v8::HandleScope handle_scope(isolate);
                if (i > 0)
                    out << " ";

                v8::Local<v8::Value> arg = args[i];
                v8::Local<v8::String> str_obj;

                if (arg->IsSymbol())
                    arg = v8::Local<v8::Symbol>::Cast(arg)->Description();

                if (!arg->ToString(currentContext).ToLocal(&str_obj))
                    continue;

                v8::String::Utf8Value str(isolate, str_obj);
                out << *str;
            }

            return out.str();
        }

      public:
        virtual void Debug(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug(Format(args, context));
        }

        virtual void Error(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(Format(args, context));
        }

        virtual void Info(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(Format(args, context));
        }

        virtual void Log(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(Format(args, context));
        }

        virtual void Warn(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(Format(args, context));
        }

        virtual void Trace(const v8::debug::ConsoleCallArguments &args, const v8::debug::ConsoleContext &context) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->trace(Format(args, context));
        }
    };
} // namespace Framework::Scripting::Builtins
