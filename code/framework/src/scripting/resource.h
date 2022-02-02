/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "init.h"

#include "engine.h"
#include "errors.h"
#include "events.h"
#include "logging/logger.h"
#include "sdk.h"
#include "utils/time.h"

#include "v8_helpers/v8_event_callback.h"
#include "v8_helpers/v8_source_location.h"
#include "v8_helpers/v8_string.h"
#include "v8_helpers/v8_try_catch.h"

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>
#include <node.h>
#include <string>
#include <unordered_map>
#include <uv.h>
#include <v8.h>
#include <v8pp/convert.hpp>
namespace Framework::Scripting {
    class Engine;
    class Resource {
      private:
        std::string _path;
        std::string _name;
        std::string _version;
        std::string _entryPoint;

        bool _loaded;
        bool _isShuttingDown;

        cppfs::FileWatcher _watcher;

        node::IsolateData *_isolateData = nullptr;
        uv_loop_t *_uvLoop              = nullptr;
        v8::Persistent<v8::Context> _context;
        v8::Persistent<v8::Script> _script;
        node::Environment *_environment = nullptr;

        Framework::Scripting::Engine *_engine = nullptr;

        std::unordered_multimap<std::string, Helpers::EventCallback> _remoteHandlers;

        SDK *_sdk;

        SDKRegisterCallback _regCb;

        Utils::Time::TimePoint _nextFileWatchUpdate;
        int32_t _fileWatchUpdatePeriod = 1000;

      public:
        Resource(Engine *, std::string &, SDKRegisterCallback);

        ~Resource();

        void Update(bool force = false);

        bool Init(SDKRegisterCallback = nullptr);
        bool Shutdown();

        bool IsLoaded() const {
            return _loaded;
        }

        std::string &GetName() {
            return _name;
        }

        v8::Local<v8::Context> GetContext();

        SDK *GetSDK() const {
            return _sdk;
        }

        void SubscribeEvent(const std::string &, v8::Local<v8::Function>, Helpers::SourceLocation &&, bool once = false);

        void InvokeErrorEvent(const std::string &, const std::string &, const std::string &, int32_t);

        template <typename... Args>
        void InvokeEvent(const std::string &eventName, bool suppressLog, Args &&... args) {
            auto entry = _remoteHandlers.find(eventName);
            if (entry == _remoteHandlers.end()) {
                if (!suppressLog)
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to find such event '{}' for resource '{}'", eventName, _name);
                return;
            }

            auto callback = &entry->second;
            if (callback->_removed) {
                Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("Event '{}' in '{}' was already fired, not supposed to be here", eventName, _name);
                return;
            }

            const auto isolate = _engine->GetIsolate();

            Helpers::TryCatch(
                [&] {
                    int const arg_count = sizeof...(Args);
                    // +1 to allocate array for arg_count == 0
                    v8::Local<v8::Value> v8_args[arg_count + 1] =
                    {
                        v8pp::to_v8(isolate, std::forward<Args>(args))...
                    };
                    v8::MaybeLocal<v8::Value> retn = callback->_fn.Get(isolate)->Call(_context.Get(isolate), v8::Undefined(isolate), arg_count, v8_args);
                    if (retn.IsEmpty()) {
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to invoke event '{}' for '{}'", eventName, _name);
                        return false;
                    }

                    if (!suppressLog)
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Successfully invoked event '{}' for '{}'", eventName, _name);
                    return true;
                },
                isolate, _context.Get(isolate));

            if (callback->_once) {
                callback->_removed = true;
            }
        };

        Engine *GetEngine() const {
            return _engine;
        }

      private:
        bool LoadPackageFile();
        bool WatchChanges();
        bool Compile(const std::string &, const std::string &);
        bool Run();
    };
} // namespace Framework::Scripting
