#pragma once

#include <map>

#include <uv.h>
#include <v8.h>
#include <v8pp/module.hpp>

#include <logging/logger.h>
#include <utils/time.h>

#include "errors.h"
#include "shared.h"
#include "v8_helpers/v8_string.h"
#include "v8_helpers/v8_try_catch.h"

namespace Framework::Scripting {
    class Engine {
      private:
      public:
        v8pp::module *_module = nullptr;

        v8::Isolate *_isolate = nullptr;
        v8::Persistent<v8::Context> _context;

        std::map<std::string, std::vector<Callback>> _eventHandlers = {};

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown() = 0;
        virtual void Update() = 0;

        bool InitSDK(SDKRegisterCallback);

        template <typename... Args>
        void InvokeEvent(const std::string name, Args... args) {
            v8::Locker locker(GetIsolate());
            v8::Isolate::Scope isolateScope(GetIsolate());
            v8::HandleScope handleScope(GetIsolate());
            v8::Context::Scope contextScope(_context.Get(_isolate));

            if (_eventHandlers[name].empty()) {
                return;
            }

            constexpr const int arg_count                           = sizeof...(Args);
            v8::Local<v8::Value> v8_args[arg_count ? arg_count : 1] = {v8pp::to_v8(_isolate, std::forward<Args>(args))...};

            for (auto it = _eventHandlers[name].begin(); it != _eventHandlers[name].end(); ++it) {
                v8::TryCatch tryCatch(_isolate);

                it->Get(_isolate)->Call(_context.Get(_isolate), v8::Undefined(_isolate), arg_count, v8_args);

                if (tryCatch.HasCaught()) {
                    const auto context                         = _context.Get(_isolate);
                    const v8::Local<v8::Message> message       = tryCatch.Message();
                    v8::Local<v8::Value> exception             = tryCatch.Exception();
                    v8::MaybeLocal<v8::String> maybeSourceLine = message->GetSourceLine(context);
                    v8::Maybe<int32_t> line                    = message->GetLineNumber(context);
                    v8::ScriptOrigin origin                    = message->GetScriptOrigin();
                    Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] exception at {}: {}: {}", name, *v8::String::Utf8Value(_isolate, origin.ResourceName()), line.ToChecked());

                    auto stackTrace = tryCatch.StackTrace(context);
                    if (!stackTrace.IsEmpty())
                        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("[Helpers] Stack trace: {}", *v8::String::Utf8Value(_isolate, stackTrace.ToLocalChecked()));
                }
            }
        }

        v8pp::module *GetModule() const {
            return _module;
        }

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        v8::Local<v8::Context> GetContext() const {
            return _context.Get(_isolate);
        }

        v8::Local<v8::ObjectTemplate> GetObjectTemplate() const {
            return _module->impl();
        }

        v8::Local<v8::Object> GetNewInstance() const {
            return _module->new_instance();
        }
    };
} // namespace Framework::Scripting