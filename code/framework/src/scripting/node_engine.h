#pragma once

// CRITICAL: Include <cerrno> BEFORE any Node.js/libuv headers on Windows.
// Node.js/libuv headers interfere with Windows SDK errno definitions,
// causing EINVAL, ERANGE to be undefined when later headers need them.
#include <cerrno>

#include "engine.h"

#include <node.h>
#include <uv.h>

#include <memory>
#include <string>
#include <vector>

namespace Framework::Scripting {

    /**
     * Configuration options for NodeEngine.
     */
    struct NodeEngineOptions {
        /**
         * Enable sandbox mode for client-side usage.
         * When enabled, dangerous APIs are disabled:
         * - fs (filesystem access)
         * - net, dgram, tls, http, https, http2 (network access)
         * - child_process (process spawning)
         * - worker_threads (threading)
         * - cluster (process clustering)
         * - os (most methods disabled)
         * - process.env (environment variables hidden)
         * - process.exit, process.kill, process.abort (process control)
         * - require.resolve paths are restricted
         */
        bool sandboxed = false;

        /**
         * Process name shown in Node.js (argv[0]).
         */
        std::string processName = "mafiahub";

        /**
         * Enable the Node.js inspector agent for debugging.
         * Only effective when compiled with FW_NODE_INSPECTOR define.
         */
        bool enableInspector = false;

        /**
         * Port for the inspector agent to listen on.
         */
        int inspectorPort = 9229;

        /**
         * Host for the inspector agent to bind to.
         */
        std::string inspectorHost = "127.0.0.1";

        /**
         * If true, pause execution at start until a debugger connects.
         */
        bool inspectorWaitForDebugger = false;
    };

    /**
     * Node.js embedded engine for scripting.
     * Can run in full mode (server) or sandboxed mode (client).
     *
     * Full mode provides complete Node.js APIs including require(), fs, http, etc.
     * Sandboxed mode restricts dangerous APIs for secure client-side execution.
     */
    class NodeEngine final : public Engine {
      public:
        explicit NodeEngine(const NodeEngineOptions &options = {});
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
         * Check if this engine is running in sandboxed mode.
         */
        bool IsSandboxed() const {
            return _options.sandboxed;
        }

        /**
         * Get the engine options.
         */
        const NodeEngineOptions &GetOptions() const {
            return _options;
        }

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

        /**
         * Get the Framework global object for binding APIs.
         */
        v8::Local<v8::Object> GetFrameworkObject() const;

      private:
        bool InitializeNode();
        bool CreateEnvironment();
        bool ApplySandbox();

        NodeEngineOptions _options;

        static std::unique_ptr<node::MultiIsolatePlatform> _platform;
        static std::shared_ptr<node::InitializationResult> _initResult;
        static bool _platformInitialized;

        // Using CommonEnvironmentSetup for proper Node.js embedding
        std::unique_ptr<node::CommonEnvironmentSetup> _setup;
        node::Environment *_env = nullptr;
        v8::Isolate *_isolate = nullptr;
    };

} // namespace Framework::Scripting
