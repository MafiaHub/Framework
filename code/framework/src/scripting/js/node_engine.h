#pragma once

#include "engine.h"

#include <node.h>
#include <uv.h>

#include <memory>
#include <string>
#include <vector>

namespace Framework::Scripting::JS {

    /**
     * Node.js embedded engine for server-side scripting.
     * Provides full Node.js APIs including require(), fs, http, etc.
     */
    class NodeEngine : public Engine {
      public:
        NodeEngine();
        ~NodeEngine() override;

        bool Init() override;
        void Shutdown() override;
        bool Execute(const std::string &code, const std::string &filename = "<eval>") override;
        bool ExecuteFile(const std::string &filepath) override;
        bool InitFrameworkSDK() override;

        /**
         * Process pending Node.js events.
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
        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        /**
         * Get the main context.
         */
        v8::Local<v8::Context> GetContext() const;

      private:
        bool InitializeNode();
        bool CreateEnvironment();

        static std::unique_ptr<node::MultiIsolatePlatform> _platform;
        static bool _platformInitialized;
        static std::vector<std::string> _nodeArgs;

        node::Environment *_env = nullptr;
        v8::Isolate *_isolate = nullptr;
        std::unique_ptr<node::IsolateData, decltype(&node::FreeIsolateData)> _isolateData;
        std::shared_ptr<node::ArrayBufferAllocator> _allocator;
        v8::Global<v8::Context> _context;
        uv_loop_t _uvLoop;
        bool _uvLoopInitialized = false;
    };

} // namespace Framework::Scripting::JS
