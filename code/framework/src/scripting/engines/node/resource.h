/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <forward_list>
#include <map>
#include <string>
#include <vector>

#include <node.h>
#include <uv.h>
#include <v8.h>
#include <v8pp/convert.hpp>

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>

#include "../engine.h"
#include "../resource.h"
#include "v8_helpers/v8_string.h"

#include "utils/time.h"

namespace Framework::Scripting::Engines::Node {
    using Callback = v8::CopyablePersistentTraits<v8::Function>::CopyablePersistent;
    class Resource: public IResource {
      private:
        v8::Isolate *_isolate;
        v8::Persistent<v8::Context> _context;
        v8::Persistent<v8::Script> _script;

        node::async_context _asyncContext {};
        v8::Persistent<v8::Object> _asyncResource;

        uv_loop_t *_uvLoop = nullptr;

        node::IsolateData *_nodeData = nullptr;
        node::Environment *_env      = nullptr;

        IEngine *_engine = nullptr;

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
        std::map<std::string, std::vector<Callback>> _eventHandlers;

      public:
        Resource(IEngine *, v8::Isolate *, std::string &);
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

        std::string &GetName() {
            return _name;
        }

        template <typename... Args>
        void InvokeEvent(const std::string name, Args &&...args) {
            const auto eventName = Helpers::MakeString(_isolate, name);

            if (_eventHandlers[name].empty()) {
                return;
            }

            int const arg_count = sizeof...(Args);
            // +1 to allocate array for arg_count == 0
            v8::Local<v8::Value> v8_args[arg_count + 1] = {v8pp::to_v8(_isolate, std::forward<Args>(args))...};

            for (auto it = _eventHandlers[name].begin(); it != _eventHandlers[name].end(); ++it) { it->Get(_isolate)->Call(_context.Get(_isolate), v8::Undefined(_isolate), arg_count, v8_args); }
        }
    };
} // namespace Framework::Scripting::Engines::Node