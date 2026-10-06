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

#include <node.h>
#include <uv.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Scripting {

    /**
     * One Node.js environment: its own isolate, libuv loop, context and Node bootstrap.
     *
     * Everything a script creates - timers, sockets, worker threads - belongs to the environment
     * that created it and dies when the runtime is destroyed, because destruction frees the
     * environment (node::FreeEnvironment stops and joins its workers) and closes its loop.
     *
     * The process-wide platform is not owned here; every runtime is created against the one the
     * engine initialised, and they all run on the thread that ticks them.
     */
    class NodeRuntime final {
      public:
        /**
         * Create the environment and run `bootstrap` as its main script.
         *
         * Only one environment in the process may own the inspector (Node asserts on a second
         * agent) and only one should own process state such as the working directory, so every
         * environment after the first is created with flags that claim neither.
         * @return The runtime, or null with the reason in `error`.
         */
        static std::unique_ptr<NodeRuntime> Create(node::MultiIsolatePlatform *platform, const std::vector<std::string> &args, const std::vector<std::string> &execArgs, node::EnvironmentFlags::Flags flags, std::string_view bootstrap, std::string &error);

        ~NodeRuntime();

        NodeRuntime(const NodeRuntime &)            = delete;
        NodeRuntime &operator=(const NodeRuntime &) = delete;

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        node::Environment *GetEnvironment() const {
            return _env;
        }

        // The caller owns the Locker / Isolate::Scope / HandleScope before calling this.
        v8::Local<v8::Context> GetContext() const;

        /**
         * Run microtasks, ready libuv callbacks and this isolate's platform tasks without blocking.
         * Enters the isolate itself.
         */
        void Tick();

      private:
        NodeRuntime() = default;

        node::MultiIsolatePlatform *_platform = nullptr;
        std::unique_ptr<node::CommonEnvironmentSetup> _setup;
        node::Environment *_env = nullptr;
        v8::Isolate *_isolate   = nullptr;
    };

} // namespace Framework::Scripting
