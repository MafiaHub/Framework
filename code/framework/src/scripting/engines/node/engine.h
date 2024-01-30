/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <map>
#include <vector>

#include <node.h>
#include <uv.h>
#include <v8.h>

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>

#include <logging/logger.h>
#include <utils/time.h>

#include "../callback.h"
#include "../engine.h"
#include "sdk.h"
#include "v8_helpers/v8_string.h"
#include "v8_helpers/v8_try_catch.h"

#define V8_RESOURCE_LOCK(engine)                                                                                       \
    v8::Locker locker(engine->GetIsolate());                                                                           \
    v8::Isolate::Scope isolateScope(engine->GetIsolate());                                                             \
    v8::HandleScope handleScope(engine->GetIsolate());                                                                 \
    v8::Context::Scope contextScope(engine->GetContext());

namespace Framework::Scripting::Engines::Node {
    using Callback = v8::CopyablePersistentTraits<v8::Function>::CopyablePersistent;
    struct GamemodeMetadata {
        std::string path;
        std::string name;
        std::string version;
        std::string entrypoint;
    };

    class Engine : public IEngine {
      private:
        SDK *_sdk = nullptr;

        // Global
        cppfs::FileWatcher *_watcher;
        Utils::Time::TimePoint _nextFileWatchUpdate;
        int32_t _fileWatchUpdatePeriod = 1000;

        // Global engine
        static inline bool _wasNodeAlreadyInited = false;
        v8::Isolate *_isolate;
        v8::Persistent<v8::ObjectTemplate> _globalObjectTemplate;
        std::unique_ptr<node::MultiIsolatePlatform> _platform;
        v8::Persistent<v8::Context> _context;
        std::string _modName;
        std::atomic<bool> _isShuttingDown = false;
        uv_loop_t uv_loop;

        // Gamemode
        std::atomic<bool> _gamemodeLoaded = false;
        std::string _gamemodePath;
        GamemodeMetadata _gamemodeMetadata = {};
        v8::Persistent<v8::Script> _gamemodeScript;
        node::IsolateData *_gamemodeData = nullptr;
        node::Environment *_gamemodeEnvironment = nullptr;
        bool _shouldReloadWatcher = false;

      public:
        std::map<std::string, std::vector<Callback>> _gamemodeEventHandlers;

      public:
        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;

        bool PreloadGamemode(std::string) override;
        bool LoadGamemode(std::string) override;
        bool UnloadGamemode(std::string) override;

        bool LoadGamemodePackageFile(std::string);
        bool CompileGamemodeScript(const std::string &, const std::string &);
        bool RunGamemodeScript();
        bool WatchGamemodeChanges(std::string);

        template <typename... Args> void InvokeEvent(const std::string name, Args... args) {
            v8::Locker locker(GetIsolate());
            v8::Isolate::Scope isolateScope(GetIsolate());
            v8::HandleScope handleScope(GetIsolate());
            v8::Context::Scope contextScope(_context.Get(_isolate));

            if (_gamemodeEventHandlers[name].empty())
            {
                return;
            }

            constexpr int const arg_count = sizeof...(Args);
            v8::Local<v8::Value> v8_args[arg_count ? arg_count : 1] = {
                v8pp::to_v8(_isolate, std::forward<Args>(args))...};

            for (auto it = _gamemodeEventHandlers[name].begin(); it != _gamemodeEventHandlers[name].end(); ++it)
            {
                v8::TryCatch tryCatch(_isolate);

                it->Get(_isolate)->Call(_context.Get(_isolate), v8::Undefined(_isolate), arg_count, v8_args);

                if (tryCatch.HasCaught())
                {
                    auto context = _context.Get(_isolate);
                    v8::Local<v8::Message> message = tryCatch.Message();
                    v8::Local<v8::Value> exception = tryCatch.Exception();
                    v8::MaybeLocal<v8::String> maybeSourceLine = message->GetSourceLine(context);
                    v8::Maybe<int32_t> line = message->GetLineNumber(context);
                    v8::ScriptOrigin origin = message->GetScriptOrigin();
                    Framework::Logging::GetInstance()
                        ->Get(FRAMEWORK_INNER_SCRIPTING)
                        ->debug("[Helpers] exception at {}: {}: {}", name,
                                *v8::String::Utf8Value(_isolate, origin.ResourceName()), line.ToChecked());

                    auto stackTrace = tryCatch.StackTrace(context);
                    if (!stackTrace.IsEmpty())
                        Framework::Logging::GetInstance()
                            ->Get(FRAMEWORK_INNER_SCRIPTING)
                            ->debug("[Helpers] Stack trace: {}",
                                    *v8::String::Utf8Value(_isolate, stackTrace.ToLocalChecked()));
                }
            }
        }

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        node::MultiIsolatePlatform *GetPlatform() const {
            return _platform.get();
        }

        v8::Local<v8::ObjectTemplate> GetGlobalObjectTemplate() const {
            return _globalObjectTemplate.Get(_isolate);
        }

        v8::Local<v8::Context> GetContext() const {
            return _context.Get(_isolate);
        }

        bool IsGamemodeLoaded() const {
            return _gamemodeLoaded;
        }

        void SetProcessArguments(int argc, char **argv) override {
        }

        void SetModName(std::string name) override {
            _modName = name;
        }

        std::string GetModName() const {
            return _modName;
        }

        std::string GetGameModeName() const {
            return _gamemodeMetadata.name;
        }

      protected:
        SDK *GetSDK() const {
            return _sdk;
        }
    };
} // namespace Framework::Scripting::Engines::Node
