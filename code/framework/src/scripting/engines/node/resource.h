/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

#include <v8.h>
#include <uv.h>
#include <node.h>

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>

#include "../engine.h"
#include "../resource.h"

#include "utils/time.h"

namespace Framework::Scripting::Engines::Node {
    class Resource : public IResource {
      private:
        v8::Isolate *_isolate;
        v8::Persistent<v8::Context> _context;
        v8::Persistent<v8::Script> _script;

        node::async_context _asyncContext{};
        v8::Persistent<v8::Object> _asyncResource;

        uv_loop_t* _uvLoop = nullptr;

        node::IsolateData* _nodeData = nullptr;
        node::Environment* _env = nullptr;

        IEngine* _engine = nullptr;

        std::string _path;
        std::string _name;
        std::string _version;
        std::string _entryPoint;

        bool _loaded;
        bool _isShuttingDown;

        cppfs::FileWatcher _watcher;
        Utils::Time::TimePoint _nextFileWatchUpdate;
        int32_t _fileWatchUpdatePeriod = 1000;
      public:
        Resource(IEngine*, v8::Isolate *, std::string &);
        ~Resource();
        bool Init() override;
        bool Shutdown() override;
        void Update(bool) override;

        bool LoadPackageFile() override;
        bool WatchChanges() override;
        bool Compile(const std::string &, const std::string &) override;
        bool Run() override;

        bool IsLoaded() override {
            return _loaded;
        }
    };
}