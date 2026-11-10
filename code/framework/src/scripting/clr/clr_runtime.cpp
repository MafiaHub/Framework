/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "clr_runtime.h"

#include "value_codec.h"

#include "../api/native_catalog.h"
#include "../builtins/builtins.h"
#include "../engine_helpers.h"
#include "../engine_scope.h"
#include "../function_references.h"
#include "../resource/resource.h"
#include "../resource/resource_manager.h"

#include <logging/logger.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace Framework::Scripting::Clr {
    namespace {
        namespace fs = std::filesystem;

        // hostfxr's own declarations (hostfxr.h, coreclr_delegates.h), restated: the server does not build against
        // the .NET SDK, it finds an installed runtime when a C# resource first starts.
#ifdef _WIN32
        using char_t = wchar_t;
#define FW_HOSTFXR_CALLTYPE __cdecl
#else
        using char_t = char;
#define FW_HOSTFXR_CALLTYPE
#endif
        using hostfxr_handle = void *;
        struct hostfxr_initialize_parameters {
            size_t size;
            const char_t *host_path;
            const char_t *dotnet_root;
        };
        constexpr int32_t kLoadAssemblyAndGetFunctionPointer = 5; // hostfxr_delegate_type::hdt_load_assembly_and_get_function_pointer
        using initialize_for_runtime_config_fn               = int32_t(FW_HOSTFXR_CALLTYPE *)(const char_t *runtimeConfigPath, const hostfxr_initialize_parameters *parameters, hostfxr_handle *handle);
        using get_runtime_delegate_fn                        = int32_t(FW_HOSTFXR_CALLTYPE *)(hostfxr_handle handle, int32_t type, void **delegate);
        using close_fn                                       = int32_t(FW_HOSTFXR_CALLTYPE *)(hostfxr_handle handle);
        using load_assembly_and_get_function_pointer_fn      = int32_t(FW_HOSTFXR_CALLTYPE *)(const char_t *assemblyPath, const char_t *typeName, const char_t *methodName, const char_t *delegateTypeName, void *reserved, void **delegate);
        const char_t *const kUnmanagedCallersOnly            = reinterpret_cast<const char_t *>(static_cast<intptr_t>(-1));

        constexpr std::string_view kHostAssembly = "MafiaHub.Framework.Scripting";
        constexpr std::string_view kEntryType    = "MafiaHub.Framework.Scripting.Interop.HostEntry, MafiaHub.Framework.Scripting";

        fs::path PathFromUtf8(std::string_view text) {
            return fs::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
        }

        std::string Utf8(const uint8_t *data, int32_t length) {
            return data && length > 0 ? std::string(reinterpret_cast<const char *>(data), static_cast<size_t>(length)) : std::string();
        }

        std::string Utf8(const Buffer &buffer) {
            return Utf8(buffer.data, buffer.length);
        }

        // What a host callback hands back: valid until the next callback on this thread.
        Buffer Hand(std::vector<uint8_t> bytes) {
            thread_local std::vector<uint8_t> storage;
            storage = std::move(bytes);
            return {storage.data(), static_cast<int32_t>(storage.size())};
        }

        Buffer HandText(const std::string &text) {
            return Hand(std::vector<uint8_t>(text.begin(), text.end()));
        }

        const uint8_t *Bytes(const std::string &text) {
            return reinterpret_cast<const uint8_t *>(text.data());
        }

        std::vector<int> ParseVersion(const std::string &name) {
            std::vector<int> parts;
            std::stringstream stream(name);
            std::string part;
            while (std::getline(stream, part, '.')) {
                try {
                    parts.push_back(std::stoi(part));
                }
                catch (...) {
                    parts.push_back(-1);
                }
            }
            return parts;
        }

        const char *HostFxrName() {
#ifdef _WIN32
            return "hostfxr.dll";
#elif defined(__APPLE__)
            return "libhostfxr.dylib";
#else
            return "libhostfxr.so";
#endif
        }

        std::vector<fs::path> DefaultRoots() {
            std::vector<fs::path> roots;
            if (const char *env = std::getenv("DOTNET_ROOT"); env && *env) {
                roots.emplace_back(env);
            }
#ifdef _WIN32
            if (const char *programFiles = std::getenv("ProgramFiles"); programFiles && *programFiles) {
                roots.push_back(fs::path(programFiles) / "dotnet");
            }
#elif defined(__APPLE__)
            roots.emplace_back("/usr/local/share/dotnet");
            roots.emplace_back("/opt/homebrew/opt/dotnet/libexec");
#else
            roots.emplace_back("/usr/share/dotnet");
            roots.emplace_back("/usr/lib/dotnet");
            roots.emplace_back("/usr/lib64/dotnet");
            roots.emplace_back("/usr/local/share/dotnet");
#endif
            if (const char *home = std::getenv("HOME"); home && *home) {
                roots.push_back(fs::path(home) / ".dotnet");
            }
            return roots;
        }

        void *OpenLibrary(const fs::path &path) {
#ifdef _WIN32
            return reinterpret_cast<void *>(::LoadLibraryW(path.c_str()));
#else
            return ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        }

        void *Symbol(void *library, const char *name) {
#ifdef _WIN32
            return reinterpret_cast<void *>(::GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
            return ::dlsym(library, name);
#endif
        }

        // .NET is per process: the managed entry point once found, and which runtime is attached to it.
        struct ProcessHost {
            InitializeFn initialize = nullptr;
            ClrRuntime *attached    = nullptr;
            uint64_t generations    = 0;
        };

        ProcessHost &Process() {
            static ProcessHost host;
            return host;
        }

        Utils::Result<InitializeFn, std::string> StartDotnet(const ClrRuntimeConfig &config) {
            using Result = Utils::Result<InitializeFn, std::string>;

            const fs::path directory     = PathFromUtf8(config.managedDirectory);
            const fs::path assembly      = directory / (std::string(kHostAssembly) + ".dll");
            const fs::path runtimeConfig = directory / (std::string(kHostAssembly) + ".runtimeconfig.json");
            std::error_code ec;
            if (config.managedDirectory.empty() || !fs::is_regular_file(assembly, ec) || !fs::is_regular_file(runtimeConfig, ec)) {
                return Result::Err(fmt::format("the C# host is missing: expected {} and {}", assembly.string(), runtimeConfig.string()));
            }

            auto located = ClrRuntime::LocateHostFxr(config.dotnetRoot);
            if (!located) {
                return Result::Err(located.GetError());
            }
            const fs::path hostfxrPath = PathFromUtf8(located.GetValue());
            const fs::path dotnetRoot  = hostfxrPath.parent_path().parent_path().parent_path().parent_path();

            void *library = OpenLibrary(hostfxrPath);
            if (!library) {
                return Result::Err("could not load " + hostfxrPath.string());
            }
            auto initialize  = reinterpret_cast<initialize_for_runtime_config_fn>(Symbol(library, "hostfxr_initialize_for_runtime_config"));
            auto getDelegate = reinterpret_cast<get_runtime_delegate_fn>(Symbol(library, "hostfxr_get_runtime_delegate"));
            auto close       = reinterpret_cast<close_fn>(Symbol(library, "hostfxr_close"));
            if (!initialize || !getDelegate || !close) {
                return Result::Err(hostfxrPath.string() + " does not export the hosting API");
            }

            const auto rootText = dotnetRoot.native();
            hostfxr_initialize_parameters parameters {sizeof(hostfxr_initialize_parameters), nullptr, rootText.c_str()};
            hostfxr_handle handle = nullptr;
            const int32_t status  = initialize(runtimeConfig.c_str(), &parameters, &handle);
            // 0 started, 1 already running in this process, 2 running with other properties: all can load the host.
            if (status < 0 || handle == nullptr) {
                return Result::Err(fmt::format("could not start .NET from {} (hostfxr 0x{:08X}); install the .NET runtime {} names", runtimeConfig.string(), static_cast<uint32_t>(status), runtimeConfig.filename().string()));
            }

            void *loader      = nullptr;
            const int32_t got = getDelegate(handle, kLoadAssemblyAndGetFunctionPointer, &loader);
            close(handle);
            if (got < 0 || loader == nullptr) {
                return Result::Err(fmt::format("could not get the .NET assembly loader (hostfxr 0x{:08X})", static_cast<uint32_t>(got)));
            }

            const auto typeName   = fs::path(std::string(kEntryType)).native();
            const auto methodName = fs::path("Initialize").native();
            void *entry           = nullptr;
            const int32_t loaded  = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loader)(assembly.c_str(), typeName.c_str(), methodName.c_str(), kUnmanagedCallersOnly, nullptr, &entry);
            if (loaded < 0 || entry == nullptr) {
                return Result::Err(fmt::format("could not load {} (0x{:08X})", assembly.string(), static_cast<uint32_t>(loaded)));
            }
            return Result::Ok(reinterpret_cast<InitializeFn>(entry));
        }
    } // namespace

    // A C# delegate as JavaScript sees it: a function in the main runtime that calls it. Weak, so JavaScript decides
    // how long it lives; the export it crossed under keeps it alive while any runtime holds it.
    struct ClrRuntime::DelegateWrapper {
        struct Data {
            uint64_t generation;
            uint64_t id;
        };

        v8::Global<v8::Function> function;
        Data *data = nullptr; // owned by the function's weak callback, or leaked with a runtime that is gone
    };

    namespace {
        CallStatus Fail(Buffer *out, const std::string &message) {
            *out = HandText(message);
            return kCallError;
        }
    } // namespace

    ClrRuntime::ClrRuntime(ResourceManager *manager, ClrRuntimeConfig config): _manager(manager), _config(std::move(config)) {
        _functions.encode = [this](const TransferredValue &function, std::vector<uint8_t> &out) {
            return EncodeFunction(function, out);
        };
        _functions.decode = [this](uint8_t tag, uint64_t id) {
            return DecodeFunction(tag, id);
        };
    }

    ClrRuntime::~ClrRuntime() {
        Shutdown();
    }

    const std::string &ClrRuntime::GetName() const {
        static const std::string name = "csharp";
        return name;
    }

    Utils::Result<std::string, std::string> ClrRuntime::LocateHostFxr(const std::string &dotnetRoot) {
        using Result = Utils::Result<std::string, std::string>;
        std::vector<fs::path> roots;
        if (!dotnetRoot.empty()) {
            roots.push_back(PathFromUtf8(dotnetRoot));
        }
        else {
            roots = DefaultRoots();
        }

        std::string searched;
        for (const auto &root : roots) {
            const fs::path fxr = root / "host" / "fxr";
            searched += (searched.empty() ? "" : ", ") + fxr.string();
            std::error_code ec;
            if (!fs::is_directory(fxr, ec)) {
                continue;
            }
            fs::path best;
            std::vector<int> bestVersion;
            for (const auto &entry : fs::directory_iterator(fxr, ec)) {
                const fs::path candidate = entry.path() / HostFxrName();
                if (!entry.is_directory(ec) || !fs::is_regular_file(candidate, ec)) {
                    continue;
                }
                auto version = ParseVersion(entry.path().filename().string());
                if (best.empty() || version > bestVersion) {
                    best        = candidate;
                    bestVersion = std::move(version);
                }
            }
            if (!best.empty()) {
                return Result::Ok(best.string());
            }
        }
        return Result::Err("no .NET installation found (looked in " + searched + "); install the .NET runtime or set DOTNET_ROOT");
    }

    Utils::Result<void, std::string> ClrRuntime::Attach() {
        using Result = Utils::Result<void, std::string>;
        if (_attached) {
            return Result::Ok();
        }
        ProcessHost &process = Process();
        if (process.attached != nullptr) {
            return Result::Err("another C# runtime is already attached in this process");
        }
        if (!process.initialize) {
            auto started = StartDotnet(_config);
            if (!started) {
                return Result::Err(started.GetError());
            }
            process.initialize = started.GetValue();
        }

        // The members a C# script calls must be in the catalog before the host resolves them.
        Builtins::RegisterNativeBindings();

        _host               = {};
        _host.version       = kInteropVersion;
        _host.context       = this;
        _host.stopGraceMs   = _config.stopGraceMs;
        _host.unloadCheckMs = _config.unloadCheckMs;
        _host.resolveNative = [](uint32_t id) {
            return Api::NativeCatalog::Get().Find(id);
        };
        _host.log             = &ClrRuntime::HostLog;
        _host.emitEvent       = &ClrRuntime::HostEmitEvent;
        _host.callExport      = &ClrRuntime::HostCallExport;
        _host.callFunction    = &ClrRuntime::HostCallFunction;
        _host.releaseFunction = &ClrRuntime::HostReleaseFunction;
        _host.settle          = &ClrRuntime::HostSettle;
        _managed              = {};
        const int32_t status  = process.initialize(&_host, &_managed);
        if (status != 0) {
            return Result::Err(fmt::format("the C# host refused to start (status {}); its version must match the server's", status));
        }
        process.attached = this;
        _generation      = ++process.generations;
        _attached        = true;
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("C# scripting started");
        return Result::Ok();
    }

    Utils::Result<void, std::string> ClrRuntime::StartResource(const Resource &resource, const std::vector<std::string> &scripts) {
        using Result = Utils::Result<void, std::string>;
        if (auto attached = Attach(); !attached) {
            return Result::Err("C# is unavailable: " + attached.GetError());
        }

        std::vector<TransferredValue> paths;
        for (const auto &script : scripts) {
            TransferredValue path;
            path.kind = TransferredValue::Kind::String;
            path.text = script;
            paths.push_back(std::move(path));
        }
        auto payload = ValueCodec::EncodeList(paths);
        if (!payload) {
            return Result::Err(payload.GetError());
        }

        const std::string &name = resource.GetName();
        Buffer out;
        if (_managed.startResource(Bytes(name), static_cast<int32_t>(name.size()), payload.GetValue().data(), static_cast<int32_t>(payload.GetValue().size()), &out) != 0) {
            return Result::Err(Utf8(out));
        }
        _loaded.insert(name);
        return Result::Ok();
    }

    void ClrRuntime::StopResource(const std::string &resourceName) {
        if (!_attached || _loaded.erase(resourceName) == 0) {
            return;
        }
        _managed.stopResource(Bytes(resourceName), static_cast<int32_t>(resourceName.size()));
    }

    bool ClrRuntime::IsResourceLoaded(const std::string &resourceName) const {
        return _loaded.contains(resourceName);
    }

    void ClrRuntime::Tick() {
        if (!_attached) {
            return;
        }
        // Wrappers JavaScript let go of since the last tick: their delegates can go too.
        for (const uint64_t id : std::exchange(_collectedDelegates, {})) {
            _delegates.erase(id);
            _managed.releaseFunction(id);
        }
        _managed.tick();
    }

    void ClrRuntime::Shutdown() {
        if (!_attached) {
            return;
        }
        // Whatever C# still owed fails rather than leaving its callers waiting.
        for (auto &[token, done] : std::exchange(_pending, {})) {
            done(CallResult::Err("C# scripting stopped"));
        }
        _managed.shutdown();
        _loaded.clear();
        _scriptFunctions.clear();
        _collectedDelegates.clear();
        if (Engine *engine = _manager->GetJSEngine(); engine && engine->IsInitialized()) {
            EngineScope scope(engine, EngineScope::Target::Host);
            for (auto &[id, wrapper] : _delegates) {
                wrapper->function.Reset();
                delete wrapper->data;
            }
        }
        _delegates.clear();
        _attached          = false;
        Process().attached = nullptr;
    }

    std::vector<UnloadingResource> ClrRuntime::GetUnloadingResources() const {
        std::vector<UnloadingResource> result;
        if (!_attached) {
            return result;
        }
        Buffer out;
        _managed.unloading(&out);
        auto entries = ValueCodec::DecodeList(out.data, static_cast<size_t>(out.length));
        if (!entries) {
            return result;
        }
        for (const auto &entry : entries.GetValue()) {
            if (entry.kind == TransferredValue::Kind::Array && entry.items.size() == 2) {
                result.push_back({entry.items[0].text, entry.items[1].boolean});
            }
        }
        return result;
    }

    bool ClrRuntime::WantsEvent(EventScope scope, std::string_view eventName) const {
        return _attached && _managed.wantsEvent(static_cast<int32_t>(scope), reinterpret_cast<const uint8_t *>(eventName.data()), static_cast<int32_t>(eventName.size())) != 0;
    }

    bool ClrRuntime::DispatchEvent(EventScope scope, const std::string &eventName, const std::string &targetResource, const std::vector<TransferredValue> &args) {
        if (!_attached) {
            return true;
        }
        auto payload = ValueCodec::EncodeList(args, &_functions);
        if (!payload) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Event '{}' did not reach C# handlers: {}", eventName, payload.GetError());
            return true;
        }
        const auto &bytes = payload.GetValue();
        return _managed.dispatchEvent(static_cast<int32_t>(scope), Bytes(eventName), static_cast<int32_t>(eventName.size()), Bytes(targetResource), static_cast<int32_t>(targetResource.size()), bytes.data(), static_cast<int32_t>(bytes.size())) == 0;
    }

    bool ClrRuntime::HasExport(const std::string &resourceName, const std::string &exportName) const {
        return _attached && _managed.hasExport(Bytes(resourceName), static_cast<int32_t>(resourceName.size()), Bytes(exportName), static_cast<int32_t>(exportName.size())) != 0;
    }

    void ClrRuntime::CallExport(const std::string &resourceName, const std::string &exportName, const std::vector<TransferredValue> &args, Completion done) {
        if (!_attached) {
            done(CallResult::Err("C# is not running"));
            return;
        }
        auto payload = ValueCodec::EncodeList(args, &_functions);
        if (!payload) {
            done(CallResult::Err(payload.GetError()));
            return;
        }
        const auto &bytes = payload.GetValue();
        Buffer out;
        uint64_t token       = 0;
        const int32_t status = _managed.callExport(Bytes(resourceName), static_cast<int32_t>(resourceName.size()), Bytes(exportName), static_cast<int32_t>(exportName.size()), bytes.data(), static_cast<int32_t>(bytes.size()), &out, &token);
        Answer(status, out, token, std::move(done));
    }

    void ClrRuntime::Answer(int32_t status, const Buffer &out, uint64_t token, Completion done) {
        switch (status) {
        case kCallOk: done(ValueCodec::Decode(out.data, static_cast<size_t>(out.length), &_functions)); return;
        case kCallPending: _pending[token] = std::move(done); return;
        default: done(CallResult::Err(Utf8(out))); return;
        }
    }

    void ClrRuntime::SettleManaged(uint64_t token, const CallResult &result) {
        if (!_attached) {
            return;
        }
        if (result) {
            std::vector<uint8_t> encoded;
            if (auto written = ValueCodec::Encode(result.GetValue(), encoded, &_functions); written) {
                _managed.settle(token, kCallOk, encoded.data(), static_cast<int32_t>(encoded.size()));
                return;
            }
            else {
                const std::string message = "the answer cannot cross into C#: " + written.GetError();
                _managed.settle(token, kCallError, Bytes(message), static_cast<int32_t>(message.size()));
                return;
            }
        }
        const std::string &message = result.GetError();
        _managed.settle(token, kCallError, Bytes(message), static_cast<int32_t>(message.size()));
    }

    int32_t ClrRuntime::AnswerManaged(fu2::function_view<void(Completion)> call, Buffer *out, uint64_t *token) {
        // Answered before `call` returns: hand it back now. Otherwise C# waits on a token we settle later.
        struct Answer {
            bool synchronous = true;
            std::optional<CallResult> result;
        };
        auto answer       = std::make_shared<Answer>();
        const uint64_t id = ++_nextHostToken;
        call([this, answer, id](CallResult result) {
            if (answer->synchronous) {
                answer->result = std::move(result);
                return;
            }
            SettleManaged(id, result);
        });
        answer->synchronous = false;

        if (!answer->result) {
            *token = id;
            return kCallPending;
        }
        if (!*answer->result) {
            return Fail(out, answer->result->GetError());
        }
        std::vector<uint8_t> encoded;
        if (auto written = ValueCodec::Encode(answer->result->GetValue(), encoded, &_functions); !written) {
            return Fail(out, "the answer cannot cross into C#: " + written.GetError());
        }
        *out = Hand(std::move(encoded));
        return kCallOk;
    }

    Utils::Result<void, std::string> ClrRuntime::EncodeFunction(const TransferredValue &function, std::vector<uint8_t> &out) {
        uint64_t id = 0;
        uint8_t tag = ValueCodec::kScriptFunctionTag;
        if (const auto delegate = DelegateOf(function)) {
            // A C# delegate coming home: C# gets the delegate itself.
            tag = ValueCodec::kManagedFunctionTag;
            id  = *delegate;
        }
        else {
            id                   = ++_nextScriptFunction;
            _scriptFunctions[id] = function;
        }
        out.push_back(tag);
        const auto *raw = reinterpret_cast<const uint8_t *>(&id);
        out.insert(out.end(), raw, raw + sizeof(id));
        return Utils::Result<void, std::string>::Ok();
    }

    namespace {
        // Marks a delegate wrapper with its Data, so the wrapper is recognised however many times it crossed.
        v8::Local<v8::Private> DelegateKey(v8::Isolate *isolate) {
            return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "framework.clrDelegate"));
        }
    } // namespace

    std::optional<uint64_t> ClrRuntime::DelegateOf(const TransferredValue &function) {
        Engine *engine = _manager->GetJSEngine();
        if (_delegates.empty() || !engine || !engine->IsInitialized()) {
            return std::nullopt;
        }
        // A wrapper exported from the host comes back there as itself.
        EngineScope scope(engine, EngineScope::Target::Host);
        v8::Isolate *isolate           = scope.Isolate();
        v8::Local<v8::Context> context = scope.Context();
        v8::TryCatch tryCatch(isolate);
        v8::Local<v8::Value> value;
        v8::Local<v8::Value> mark;
        if (!ValueTransfer::Rebuild(isolate, context, function, _manager->GetMainTransferFunctions()).ToLocal(&value) || !value->IsFunction() || !value.As<v8::Function>()->GetPrivate(context, DelegateKey(isolate)).ToLocal(&mark) || !mark->IsExternal()) {
            return std::nullopt;
        }
        const auto *data = static_cast<DelegateWrapper::Data *>(mark.As<v8::External>()->Value());
        return data->generation == _generation ? std::optional<uint64_t>(data->id) : std::nullopt;
    }

    Utils::Result<TransferredValue, std::string> ClrRuntime::DecodeFunction(uint8_t tag, uint64_t id) {
        using Result = Utils::Result<TransferredValue, std::string>;
        if (tag == ValueCodec::kManagedFunctionTag) {
            return WrapDelegate(id);
        }
        const auto it = _scriptFunctions.find(id);
        if (it == _scriptFunctions.end()) {
            return Result::Err("a JavaScript function C# had released");
        }
        return Result::Ok(it->second);
    }

    Utils::Result<TransferredValue, std::string> ClrRuntime::WrapDelegate(uint64_t id) {
        using Result     = Utils::Result<TransferredValue, std::string>;
        Engine *engine   = _manager->GetJSEngine();
        const auto cross = _manager->GetMainTransferFunctions();
        if (!engine || !engine->IsInitialized() || !cross.exportFunction) {
            return Result::Err("a C# function can only reach JavaScript when resources run in runtimes of their own");
        }
        EngineScope scope(engine, EngineScope::Target::Host);
        v8::Isolate *isolate           = scope.Isolate();
        v8::Local<v8::Context> context = scope.Context();

        auto &wrapper = _delegates[id];
        if (!wrapper) {
            wrapper = std::make_unique<DelegateWrapper>();
        }
        v8::Local<v8::Function> function;
        if (!wrapper->function.IsEmpty()) {
            function = wrapper->function.Get(isolate);
        }
        else {
            wrapper->data = new DelegateWrapper::Data {_generation, id};
            function      = v8::Function::New(context, &ClrRuntime::DelegateCallback, v8::External::New(isolate, wrapper->data)).ToLocalChecked();
            function->SetPrivate(context, DelegateKey(isolate), v8::External::New(isolate, wrapper->data)).Check();
            wrapper->function.Reset(isolate, function);
            wrapper->function.SetWeak(
                wrapper->data,
                [](const v8::WeakCallbackInfo<DelegateWrapper::Data> &info) {
                    DelegateWrapper::Data *data = info.GetParameter();
                    ProcessHost &process        = Process();
                    if (process.attached && process.attached->_generation == data->generation) {
                        ClrRuntime *runtime = process.attached;
                        if (auto it = runtime->_delegates.find(data->id); it != runtime->_delegates.end()) {
                            it->second->function.Reset();
                            it->second->data = nullptr;
                        }
                        runtime->_collectedDelegates.push_back(data->id);
                    }
                    delete data;
                },
                v8::WeakCallbackType::kParameter);
        }

        v8::TryCatch tryCatch(isolate);
        auto copied = ValueTransfer::Copy(isolate, context, function, cross);
        if (!copied) {
            return Result::Err(copied.GetError());
        }
        return Result::Ok(std::move(copied).GetValue());
    }

    void ClrRuntime::DelegateCallback(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate           = info.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const auto *data               = static_cast<DelegateWrapper::Data *>(info.Data().As<v8::External>()->Value());
        ProcessHost &process           = Process();
        ClrRuntime *self               = process.attached;
        if (!self || self->_generation != data->generation) {
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "this C# function belongs to a C# scripting run that has stopped")));
            return;
        }

        const TransferFunctions functions = self->_manager->GetMainTransferFunctions();
        std::vector<TransferredValue> args;
        for (int i = 0; i < info.Length(); ++i) {
            auto value = ValueTransfer::Copy(isolate, context, info[i], functions);
            if (!value) {
                ThrowTypeError(isolate, ("argument " + std::to_string(i + 1) + " cannot cross into C#: " + value.GetError()).c_str());
                return;
            }
            args.push_back(std::move(value).GetValue());
        }
        auto payload = ValueCodec::EncodeList(args, &self->_functions);
        if (!payload) {
            ThrowTypeError(isolate, payload.GetError().c_str());
            return;
        }

        const uint64_t id = data->id;
        v8::Local<v8::Value> answer;
        const bool answered = self->_manager
                                  ->AwaitInJavaScript(isolate, context,
                                      [&](Completion done) {
                                          Buffer out;
                                          uint64_t token       = 0;
                                          const auto &bytes    = payload.GetValue();
                                          const int32_t status = self->_managed.invokeFunction(id, bytes.data(), static_cast<int32_t>(bytes.size()), &out, &token);
                                          self->Answer(status, out, token, std::move(done));
                                      })
                                  .ToLocal(&answer);
        if (answered) {
            info.GetReturnValue().Set(answer);
        }
    }

    int32_t ClrRuntime::HostEmitEvent(void *context, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength, Buffer *out) {
        auto *self = static_cast<ClrRuntime *>(context);
        auto args  = ValueCodec::DecodeList(payload, static_cast<size_t>(payloadLength), &self->_functions);
        if (!args) {
            return Fail(out, args.GetError());
        }
        auto emitted = self->_manager->EmitFromRuntime(Utf8(name, nameLength), Utf8(target, targetLength), args.GetValue());
        if (!emitted) {
            return Fail(out, emitted.GetError());
        }
        return kCallOk;
    }

    int32_t ClrRuntime::HostCallExport(void *context, const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token) {
        auto *self = static_cast<ClrRuntime *>(context);
        auto args  = ValueCodec::DecodeList(payload, static_cast<size_t>(payloadLength), &self->_functions);
        if (!args) {
            return Fail(out, args.GetError());
        }
        const std::string resourceName = Utf8(resource, resourceLength);
        const std::string exportName   = Utf8(name, nameLength);
        return self->AnswerManaged(
            [&](Completion done) {
                self->_manager->CallExport(resourceName, exportName, args.GetValue(), std::move(done));
            },
            out, token);
    }

    int32_t ClrRuntime::HostCallFunction(void *context, uint64_t handle, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token) {
        auto *self          = static_cast<ClrRuntime *>(context);
        const auto function = self->_scriptFunctions.find(handle);
        if (function == self->_scriptFunctions.end()) {
            return Fail(out, "the JavaScript function was released");
        }
        auto args = ValueCodec::DecodeList(payload, static_cast<size_t>(payloadLength), &self->_functions);
        if (!args) {
            return Fail(out, args.GetError());
        }
        const TransferredValue callee = function->second;
        return self->AnswerManaged(
            [&](Completion done) {
                self->_manager->CallFunction(callee, args.GetValue(), std::move(done));
            },
            out, token);
    }

    void ClrRuntime::HostReleaseFunction(void *context, uint64_t handle) {
        static_cast<ClrRuntime *>(context)->_scriptFunctions.erase(handle);
    }

    void ClrRuntime::HostSettle(void *context, uint64_t token, int32_t status, const uint8_t *payload, int32_t payloadLength) {
        auto *self    = static_cast<ClrRuntime *>(context);
        const auto it = self->_pending.find(token);
        if (it == self->_pending.end()) {
            return;
        }
        Completion done = std::move(it->second);
        self->_pending.erase(it);
        if (status == kCallOk) {
            done(ValueCodec::Decode(payload, static_cast<size_t>(payloadLength), &self->_functions));
        }
        else {
            done(CallResult::Err(Utf8(payload, payloadLength)));
        }
    }

    void ClrRuntime::HostLog(void *, int32_t level, const uint8_t *resource, int32_t resourceLength, const uint8_t *message, int32_t messageLength) {
        const std::string owner = Utf8(resource, resourceLength);
        const std::string text  = Utf8(message, messageLength);
        auto logger             = Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING);
        switch (level) {
        case 0: logger->debug("[{}] {}", owner, text); break;
        case 2: logger->warn("[{}] {}", owner, text); break;
        case 3: logger->error("[{}] {}", owner, text); break;
        default: logger->info("[{}] {}", owner, text); break;
        }
    }
} // namespace Framework::Scripting::Clr
