#pragma once

#include "engine.h"

#include <node.h>
#include <uv.h>

#include <memory>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Node.js embedded engine for server-side scripting.
     * Provides full Node.js APIs including require(), fs, http, etc.
     */
    class NodeEngine final : public Engine {
      public:
        NodeEngine();
        ~NodeEngine() override;

        bool Init() override;
        void Shutdown() override;
        bool Execute(const std::string &code, const std::string &filename = "<eval>") override;
        bool ExecuteFile(const std::string &filepath) override;
        bool InitFrameworkSDK() override;

        /**
         * Process pending Node.js events (non-blocking).
         * Call this from game loop to process async operations.
         */
        void Tick();

        /**
         * Get the Node.js environment.
         */
        node::Environment *GetEnvironment() const {
            return _env;
        }

        /**
         * Get the V8 isolate (Node.js uses V8 internally).
         */
        v8::Isolate *GetIsolate() const override {
            return _isolate;
        }

        /**
         * Get the main context.
         */
        v8::Local<v8::Context> GetContext() const override;

      private:
        bool InitializeNode();
        bool CreateEnvironment();

        static std::unique_ptr<node::MultiIsolatePlatform> _platform;
        static std::shared_ptr<node::InitializationResult> _initResult;
        static bool _platformInitialized;
        static std::vector<std::string> _nodeArgs;

        // Using CommonEnvironmentSetup for proper Node.js embedding
        std::unique_ptr<node::CommonEnvironmentSetup> _setup;
        node::Environment *_env = nullptr;
        v8::Isolate *_isolate = nullptr;
    };

} // namespace Framework::Scripting
