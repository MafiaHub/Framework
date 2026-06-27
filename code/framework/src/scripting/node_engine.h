/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

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

        [[nodiscard]] ScriptingError Init() override;
        void Shutdown() override;
        bool ExecuteFile(std::string_view filepath) override;

        // Evict CommonJS modules cached under rootPath (require.cache).
        void EvictModulesUnderPath(const std::string &rootPath) override;

        // Cancel timers the named resource created (via the bootstrap shim).
        void ClearResourceTimers(const std::string &resourceName) override;

        // Install the privileged __fw_ownerOf(fn) helper for the timer shim.
        // Call once after Init() with V8 scopes active.
        void InstallResourceTimerTracking();

        /**
         * Process pending Node.js events (non-blocking).
         * Call this from game loop to process async operations.
         */
        void Tick();

        /**
         * Pending uncaught error from process.on('uncaughtException') or
         * process.on('unhandledRejection'), queued during Tick() for
         * deferred processing outside of V8/libuv callbacks.
         */
        struct PendingUncaughtError {
            std::string resourceName;
            std::string errorMessage;
        };

        /**
         * Install the C++ handler for uncaught exceptions/rejections.
         * The bootstrap already installs JS process handlers that prevent crashes.
         * This method connects them to a C++ callback for error attribution.
         * Must be called with V8 scopes active.
         * @param resourcesPath Path to resources directory for extracting resource names from stacks
         */
        void InstallUncaughtExceptionHandler(const std::string &resourcesPath);

        /**
         * Drain pending uncaught errors. Returns and clears the queue.
         * Call after Tick() to process errors outside of V8/libuv callbacks.
         */
        std::vector<PendingUncaughtError> DrainPendingErrors();

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

      private:
        bool InitializeNode();
        bool CreateEnvironment();
        bool ApplySandbox();

        static void OnUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info);

        // __fw_ownerOf(fn): resource owning a function, from its script origin.
        static void OnTimerOwnerLookup(const v8::FunctionCallbackInfo<v8::Value> &info);

        NodeEngineOptions _options;

        static std::unique_ptr<node::MultiIsolatePlatform> _platform;
        static std::shared_ptr<node::InitializationResult> _initResult;
        static bool _platformInitialized;

        // Using CommonEnvironmentSetup for proper Node.js embedding
        std::unique_ptr<node::CommonEnvironmentSetup> _setup;
        node::Environment *_env = nullptr;
        v8::Isolate *_isolate = nullptr;

        // Cached JS function that calls setImmediate(()=>{}) each tick.
        // This serves two purposes for inspector CDP message processing:
        // 1. Enters JS execution, triggering V8 safepoint where pending
        //    interrupts (queued via RequestInterrupt) are drained.
        // 2. Activates Node's CheckImmediate uv_check handle, which calls
        //    RunAndClearNativeImmediates → RunAndClearInterrupts internally.
        // Only created when FW_NODE_INSPECTOR is defined and inspector enabled.
        v8::Global<v8::Function> _interruptDrainFn;

        // Pending uncaught errors queued during Tick() for deferred processing
        std::vector<PendingUncaughtError> _pendingErrors;
        // Canonical resources path for extracting resource names from error stacks
        std::string _resourcesPath;
    };

} // namespace Framework::Scripting
