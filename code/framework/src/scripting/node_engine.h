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
#include "function_references.h"
#include "node_runtime.h"

#include <node.h>
#include <uv.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
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

        /**
         * Process pending Node.js events (non-blocking).
         * Call this from game loop to process async operations.
         */
        void Tick() override;

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
         * Create a further environment on the platform this engine initialised, bootstrapped the
         * same way as the engine's own: its own isolate, loop and context.
         * @return The runtime, or null with the reason in `error`.
         */
        std::unique_ptr<NodeRuntime> CreateRuntime(std::string &error) const;

        /**
         * Get the Node.js environment.
         */
        node::Environment *GetEnvironment() const {
            return _runtime ? _runtime->GetEnvironment() : nullptr;
        }

        /**
         * The isolate of the runtime this thread is executing in: a resource's own while inside it, the
         * engine's otherwise. Native code reached from a resource therefore builds its values in that
         * resource's isolate.
         */
        v8::Isolate *GetIsolate() const override;

        /**
         * The context of the runtime GetIsolate() names.
         */
        v8::Local<v8::Context> GetContext() const override;

        // One runtime per resource: see docs/scripting_resource_isolation.md.
        bool CreateResourceRuntime(const std::string &resourceName) override;
        void DestroyResourceRuntime(const std::string &resourceName) override;
        bool ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) override;
        std::string GetResourceForIsolate(v8::Isolate *isolate) const override;
        FunctionReferences *GetFunctionReferences() const override {
            return _references.get();
        }

        // The runtime a running resource executes in, or null. For diagnostics and tests.
        NodeRuntime *GetResourceRuntime(const std::string &resourceName) const;

      private:
        bool InitializeNode();
        bool CreateEnvironment();
        // Locks down the runtime `context` belongs to. The caller holds its scopes.
        bool ApplySandbox(v8::Local<v8::Context> context);

        // An uncaught error's text, as the bootstrap's handlers pass it: "[origin] message".
        static std::string ReadUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void OnUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info);

        // A resource's runtime reports its uncaught errors here, already attributed to it.
        static void OnResourceUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info);

        struct ResourceRuntime {
            std::string resourceName;
            std::unique_ptr<NodeRuntime> runtime;
            uint32_t reference = 0;
            uint64_t serial    = 0;       // Tells this runtime from a later one of the same resource.
            NodeEngine *engine = nullptr; // What the runtime's error sink reports to.
            bool exited        = false;   // It called process.exit(): stopped, and no longer ticked.
            bool closed        = false;   // Since then cut off from other runtimes and from the builtins.
        };

        // A resource that called process.exit(), to be stopped at the next tick.
        struct PendingExit {
            std::string resourceName;
            uint64_t serial = 0;
            int code        = 0;
        };

        // Leave the references and free the environment. The runtime must not be executing.
        void TeardownResourceRuntime(std::unique_ptr<ResourceRuntime> runtime);

        // Whether a runtime can be freed now: nothing runs in any isolate on this thread, so no native code holds a
        // borrowed handle into it.
        bool CanTeardownNow(const ResourceRuntime &runtime) const;

        // Destroy the runtimes whose destruction was deferred because script was running.
        void FlushRetiredRuntimes();

        // Stop the resources that called process.exit() since the last tick.
        void StopExitedResources();

        // Cut a runtime that called process.exit() off from the others and from the builtins' tables, ahead of its
        // teardown. The runtime must not be executing.
        void CloseExitedRuntime(ResourceRuntime &runtime);

        // Tick every resource runtime that is not executing further up the stack.
        void TickResourceRuntimes();

        NodeEngineOptions _options;

        static std::unique_ptr<node::MultiIsolatePlatform> _platform;
        static std::shared_ptr<node::InitializationResult> _initResult;
        static bool _platformInitialized;

        std::unique_ptr<NodeRuntime> _runtime;

        // Calls and values between the engine's runtime and every resource's.
        std::unique_ptr<FunctionReferences> _references;
        uint32_t _hostReference = 0;

        // One runtime per running resource, and the isolate each one owns.
        std::map<std::string, std::unique_ptr<ResourceRuntime>, std::less<>> _resourceRuntimes;
        std::unordered_map<v8::Isolate *, ResourceRuntime *> _runtimeByIsolate;

        // Runtimes stopped while script was running; destroyed at the next tick.
        std::vector<std::unique_ptr<ResourceRuntime>> _retiredRuntimes;

        std::vector<PendingExit> _pendingExits;
        uint64_t _nextRuntimeSerial = 1;

        // Bumped whenever a resource runtime is created or destroyed, so a walk over them sees the change.
        uint64_t _runtimesGeneration = 0;

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
