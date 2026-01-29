#pragma once

#include "engine.h"

#include <v8.h>
#include <libplatform/libplatform.h>

#include <memory>
#include <string>

namespace Framework::Scripting {

    /**
     * Pure V8 JavaScript engine for client-side scripting.
     * Provides sandboxed execution with Framework APIs exposed.
     */
    class V8Engine final : public Engine {
      public:
        V8Engine();
        ~V8Engine() override;

        bool Init() override;
        void Shutdown() override;
        bool Execute(const std::string &code, const std::string &filename = "<eval>") override;
        bool ExecuteFile(const std::string &filepath) override;
        bool InitFrameworkSDK() override;

        /**
         * Get the V8 isolate for this engine.
         */
        v8::Isolate *GetIsolate() const override {
            return _isolate;
        }

        /**
         * Get the global context.
         */
        v8::Local<v8::Context> GetContext() const override {
            return _context.Get(_isolate);
        }

        /**
         * Get the Framework global object for binding APIs.
         */
        v8::Local<v8::Object> GetFrameworkObject() const;

      private:
        bool InitializePlatform();
        bool CreateIsolate();
        bool CreateContext();
        void SetupSandbox();

        std::string FormatException(v8::TryCatch &tryCatch);

        static std::unique_ptr<v8::Platform> _platform;
        static bool _platformInitialized;

        v8::Isolate *_isolate = nullptr;
        v8::Global<v8::Context> _context;
        v8::Isolate::CreateParams _createParams;
    };

} // namespace Framework::Scripting
