#pragma once

#include <v8.h>

#include <memory>
#include <string>
#include <functional>

namespace Framework::Scripting {

    /**
     * Abstract base class for JavaScript engines.
     * Provides common interface for both V8 (client) and libnode (server).
     */
    class Engine {
      public:
        using SDKRegisterCallback = std::function<void(Engine *)>;

        Engine() = default;
        virtual ~Engine() = default;

        // Non-copyable
        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;

        /**
         * Initialize the JavaScript engine.
         * @return true if initialization succeeded
         */
        virtual bool Init() = 0;

        /**
         * Shutdown the JavaScript engine.
         */
        virtual void Shutdown() = 0;

        /**
         * Execute a JavaScript string.
         * @param code JavaScript code to execute
         * @param filename Optional filename for error messages
         * @return true if execution succeeded
         */
        virtual bool Execute(const std::string &code, const std::string &filename = "<eval>") = 0;

        /**
         * Execute a JavaScript file.
         * @param filepath Path to the JavaScript file
         * @return true if execution succeeded
         */
        virtual bool ExecuteFile(const std::string &filepath) = 0;

        /**
         * Register framework SDK bindings.
         * Called after Init() to set up Framework.* APIs.
         */
        virtual bool InitFrameworkSDK() = 0;

        /**
         * Set callback for registering additional SDK bindings.
         * Used by game projects to add custom APIs.
         */
        void SetSDKRegisterCallback(SDKRegisterCallback callback) {
            _sdkRegisterCallback = std::move(callback);
        }

        /**
         * Get the last error message.
         */
        const std::string &GetLastError() const {
            return _lastError;
        }

        /**
         * Check if engine is initialized.
         */
        bool IsInitialized() const {
            return _initialized;
        }

        /**
         * Get the V8 isolate.
         */
        virtual v8::Isolate *GetIsolate() const = 0;

        /**
         * Get the main context.
         */
        virtual v8::Local<v8::Context> GetContext() const = 0;

      protected:
        bool _initialized = false;
        std::string _lastError;
        SDKRegisterCallback _sdkRegisterCallback;
    };

} // namespace Framework::Scripting
