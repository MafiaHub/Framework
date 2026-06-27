/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// CRITICAL: Include <cerrno> BEFORE V8 headers on Windows.
// V8 headers include Windows SDK headers that can interfere with
// errno definitions (EINVAL, ERANGE, etc.) needed by <string> and others.
#include <cerrno>

#include "engine.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {

    /**
     * Configuration options for V8Engine.
     */
    struct V8EngineOptions {
        /**
         * Process name shown in V8 (argv[0]).
         */
        std::string processName = "mafiahub-client";

        /**
         * Optional root directory for require() path sandboxing.
         * If set, require() cannot resolve paths outside this directory.
         */
        std::string moduleRootPath;
    };

    /**
     * Standalone V8 engine for client-side scripting.
     * Uses V8 directly without Node.js runtime — no fs, net, child_process,
     * or any other Node.js APIs are available at the binary level.
     *
     * Provides:
     * - CommonJS require() for local .js files
     * - setTimeout / setInterval / clearTimeout / clearInterval
     * - queueMicrotask
     * - globalThis.Framework object for SDK bindings
     */
    class V8Engine final : public Engine {
      public:
        explicit V8Engine(const V8EngineOptions &options = {});
        ~V8Engine() override;

        [[nodiscard]] ScriptingError Init() override;
        void Shutdown() override;
        bool ExecuteFile(std::string_view filepath) override;

        /**
         * Process pending timers and microtasks.
         * Call this from game loop to process async operations.
         */
        void Tick();

        v8::Isolate *GetIsolate() const override;
        v8::Local<v8::Context> GetContext() const override;

        /**
         * Update the module root path for require() sandboxing.
         * Call this when the resource cache path changes.
         */
        void SetModuleRootPath(const std::string &path);

        /**
         * Clear cached modules and loader callback data.
         * Call this after stopping resources to avoid stale modules on reconnect.
         */
        void ClearModuleCache();

        /**
         * Evict cached modules (and their require() backing data) whose
         * resolved path sits inside rootPath. Used by hot-reload; call only
         * after the owning resource has been stopped.
         */
        void EvictModulesUnderPath(const std::string &rootPath) override;

        /**
         * Cancel all timers created by the named resource. Call after stop.
         */
        void ClearResourceTimers(const std::string &resourceName) override;

        // Module loader internals (used by require callback)
        v8::MaybeLocal<v8::Value> LoadModule(std::string_view requestedPath,
                                              std::string_view referencingDir);
        std::string ResolveModulePath(std::string_view requested,
                                       std::string_view fromDir);

        // Timer entry (public for anonymous-namespace callbacks)
        struct TimerEntry {
            uint32_t id = 0;
            v8::Global<v8::Function> callback;
            std::vector<v8::Global<v8::Value>> args;
            std::chrono::steady_clock::time_point fireTime;
            int intervalMs = 0; // 0 = setTimeout, >0 = setInterval
            bool cancelled = false;
            std::string ownerResource; // resource that created the timer, for cleanup on stop
        };

        static constexpr uint32_t kMaxTimers = 10000;

        uint32_t AddTimer(std::unique_ptr<TimerEntry> entry);
        void CancelTimer(uint32_t id);

        // Data structs for V8 callbacks (public for anonymous-namespace callbacks)
        struct RequireData {
            V8Engine *engine;
            std::string currentDir;
        };

        struct TimerCallbackData {
            V8Engine *engine;
            bool isInterval;
        };

      private:
        bool InitializePlatform();
        bool CreateIsolateAndContext();
        void InstallGlobals();
        void InstallTimerFunctions();
        void InstallRequireFunction();
        void ProcessTimers();

        V8EngineOptions _options;

        static std::unique_ptr<v8::Platform> _platform;
        static bool _platformInitialized;

        v8::Isolate *_isolate = nullptr;
        v8::Isolate::CreateParams _createParams;
        v8::Global<v8::Context> _context;

        // Timer queue
        std::vector<std::unique_ptr<TimerEntry>> _timers;
        uint32_t _nextTimerId = 1;

        // Timer callback data (owned, lifetime matches engine)
        TimerCallbackData _setTimeoutData;
        TimerCallbackData _setIntervalData;

        // Module cache (keyed by resolved absolute path)
        std::map<std::string, v8::Global<v8::Value>> _moduleCache;

        // RequireData instances (owned, cleaned up in Shutdown)
        std::vector<std::unique_ptr<RequireData>> _requireDataStore;
    };

} // namespace Framework::Scripting
