/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "init.h"
#include "sdk.h"
#include "utils/time.h"
#include "v8_helpers/v8_event_callback.h"
#include "v8_helpers/v8_source_location.h"

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>
#include <node.h>
#include <string>
#include <unordered_map>
#include <uv.h>
#include <v8.h>

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

        void InvokeEvent(const std::string &, std::vector<v8::Local<v8::Value>> &, bool suppressLog = false);

      private:
        bool LoadPackageFile();
        bool WatchChanges();
        bool Compile(const std::string &, const std::string &);
        bool Run();
    };
} // namespace Framework::Scripting
