/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "console.h"
#include "../lua/lua_runtime.h"
#include "../scripting_catalog.h"

#include <memory>
#include <sstream>

namespace Framework::Scripting::Builtins {

    namespace {
        // What every function of the console does: the arguments, formatted and joined with spaces, as one line of
        // the output of the runtime.
        void Write(Bind::Call &call, OutputLevel level) {
            std::string message;
            for (size_t i = 0; i < call.ArgCount(); ++i) {
                if (i > 0) {
                    message += ' ';
                }
                message += Console::FormatValue(call.Arg(i));
            }
            call.WriteOutput(level, message);
        }

        Bind::NativeFunction WriteAt(OutputLevel level) {
            return [level](Bind::Call &call) {
                Write(call, level);
            };
        }

        Bind::Doc Document(const char *description) {
            return Bind::Doc(description).RestParam("values", Bind::Type::Custom("unknown[]", "any"), "Values formatted and joined with spaces.");
        }
    } // namespace

    const Bind::Namespace &Console::Definition() {
        static const auto definition = Bind::Namespace("console", "Resource-aware console that routes output through the Framework logger.")
                                           .Function("log", WriteAt(OutputLevel::Info), Document("Writes an informational log entry prefixed with the current resource name."))
                                           .Function("info", WriteAt(OutputLevel::Info), Document("Alias of console.log for informational output."))
                                           .Function("warn", WriteAt(OutputLevel::Warning), Document("Writes a warning log entry prefixed with the current resource name."))
                                           .Function("error", WriteAt(OutputLevel::Error), Document("Writes an error log entry prefixed with the current resource name."))
                                           .Function("debug", WriteAt(OutputLevel::Debug), Document("Writes a debug log entry prefixed with the current resource name."));
        return definition;
    }

    void Console::Register(v8::Isolate *isolate, v8::Local<v8::Context> context, ResourceManager *resourceManager) {
        // The functions carry no manager of their own: a call asks the one of its runtime for its resource.
        if (resourceManager != nullptr) {
            Bind::SetCallResourceManager(isolate, resourceManager);
        }
        Definition().MaterialiseV8(isolate, context, context->Global(), GetScriptingCatalog(isolate), ScriptingSide(isolate));
    }

    void Console::RegisterLua(LuaRuntime &runtime) {
        Definition().RegisterLuaGlobal(runtime.State(), runtime.Options().side);
    }

    std::string Console::FormatValue(const Bind::Value &value) {
        switch (value.Kind()) {
        case Bind::ValueKind::String: return value.AsString();
        case Bind::ValueKind::Number: {
            if (const auto integer = value.AsInteger()) {
                return std::to_string(*integer);
            }
            std::ostringstream ss;
            ss << value.AsNumber();
            return ss.str();
        }
        case Bind::ValueKind::Boolean: return value.AsBoolean() ? "true" : "false";
        case Bind::ValueKind::Array: return "[Array(" + std::to_string(value.Length()) + ")]";
        case Bind::ValueKind::Function: return "[Function]";
        case Bind::ValueKind::Object:
        case Bind::ValueKind::Bound: {
            // A circular structure, or anything else that has no JSON, is only named.
            const auto json = value.ToJson();
            return json ? json.GetValue() : "[Object]";
        }
        // The language spells what it has for nothing: null and undefined in JavaScript, nil in Lua.
        case Bind::ValueKind::Nil:
        case Bind::ValueKind::Other: break;
        }
        std::string spelled = value.ToDisplayString();
        return spelled.empty() ? "<unknown>" : spelled;
    }

    std::string Console::FormatArgs(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args) {
        std::string joined;
        for (int i = 0; i < args.Length(); ++i) {
            if (i > 0) {
                joined += ' ';
            }
            joined += FormatValue(isolate, args[i]);
        }
        return joined;
    }

    std::string Console::FormatValue(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        // The formatter reads a value of a call, so the value is handed to a function made for it.
        v8::HandleScope scope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const auto formatted           = std::make_shared<std::string>();
        v8::Local<v8::Function> format = Bind::MakeV8Function(isolate, context, [formatted](Bind::Call &call) {
            *formatted = FormatValue(call.Arg(0));
        });
        v8::TryCatch tryCatch(isolate);
        (void)format->Call(context, v8::Undefined(isolate), 1, &value);
        return *formatted;
    }

} // namespace Framework::Scripting::Builtins
