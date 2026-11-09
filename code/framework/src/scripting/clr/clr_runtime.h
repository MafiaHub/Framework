/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../script_runtime.h"
#include "clr_interop.h"
#include "value_codec.h"

#include <v8.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::Scripting::Clr {
    struct ClrRuntimeConfig {
        // Directory holding MafiaHub.Framework.Scripting.dll and its runtimeconfig.json.
        std::string managedDirectory;

        // The .NET install to run on. Empty: DOTNET_ROOT, then the platform's default install locations.
        std::string dotnetRoot;

        // How long a stopping resource's background work (ServerScript.Background) may take to end.
        uint32_t stopGraceMs = 1000;

        // How long after a stop a resource's assemblies may stay loaded before it is reported as still running.
        uint32_t unloadCheckMs = 10000;
    };

    // A stopped resource whose assemblies are still loaded.
    struct UnloadingResource {
        std::string name;
        // Still loaded after unloadCheckMs: something it started is still running, and was reported.
        bool overdue = false;
    };

    /**
     * Server resources written in C#, on CoreCLR (docs/scripting_csharp.md). Selected with "serverRuntime": "csharp".
     *
     * .NET starts with the first C# resource, so a server without one never loads it, and stays loaded for the life
     * of the process: CoreCLR cannot be unloaded or started twice. Each resource gets a collectible
     * AssemblyLoadContext that is unloaded when it stops. Every call into C# comes from the scripting thread; C#
     * continuations run on it too, from Tick().
     */
    class ClrRuntime final: public ScriptRuntime {
      public:
        ClrRuntime(ResourceManager *manager, ClrRuntimeConfig config);
        ~ClrRuntime() override;

        ClrRuntime(const ClrRuntime &)            = delete;
        ClrRuntime &operator=(const ClrRuntime &) = delete;

        const std::string &GetName() const override;

        Utils::Result<void, std::string> StartResource(const Resource &resource, const std::vector<std::string> &scripts) override;
        void StopResource(const std::string &resourceName) override;
        bool IsResourceLoaded(const std::string &resourceName) const override;
        void Tick() override;
        void Shutdown() override;
        bool WantsEvent(EventScope scope, std::string_view eventName) const override;
        bool DispatchEvent(EventScope scope, const std::string &eventName, const std::string &targetResource, const std::vector<TransferredValue> &args) override;
        bool HasExport(const std::string &resourceName, const std::string &exportName) const override;
        void CallExport(const std::string &resourceName, const std::string &exportName, const std::vector<TransferredValue> &args, Completion done) override;

        // Whether this runtime has started .NET and attached to the managed host.
        bool IsAttached() const {
            return _attached;
        }

        // Stopped C# resources whose assemblies have not unloaded yet.
        std::vector<UnloadingResource> GetUnloadingResources() const;

        // The hostfxr library this configuration would start, or an error saying where it looked.
        static Utils::Result<std::string, std::string> LocateHostFxr(const std::string &dotnetRoot);

      private:
        struct DelegateWrapper;

        Utils::Result<void, std::string> Attach();

        // Functions crossing in the codec: JavaScript ones kept while C# holds them, C# ones wrapped in the main runtime.
        Utils::Result<void, std::string> EncodeFunction(const TransferredValue &function, std::vector<uint8_t> &out);
        Utils::Result<TransferredValue, std::string> DecodeFunction(uint8_t tag, uint64_t id);
        Utils::Result<TransferredValue, std::string> WrapDelegate(uint64_t id);

        // The delegate id of a function that is one of this run's wrappers.
        std::optional<uint64_t> DelegateOf(const TransferredValue &function);

        // What a managed call answered: now, or later through HostSettle.
        void Answer(int32_t status, const Buffer &out, uint64_t token, Completion done);

        // Report a completion to C# under a token it is waiting on.
        void SettleManaged(uint64_t token, const CallResult &result);

        // Run `call` with a completion; report its answer through `out`/`token`, now or later.
        int32_t AnswerManaged(fu2::function_view<void(Completion)> call, Buffer *out, uint64_t *token);

        static void DelegateCallback(const v8::FunctionCallbackInfo<v8::Value> &info);
        static int32_t HostCallFunction(void *context, uint64_t handle, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token);
        static void HostReleaseFunction(void *context, uint64_t handle);
        static void HostSettle(void *context, uint64_t token, int32_t status, const uint8_t *payload, int32_t payloadLength);

        static int32_t HostEmitEvent(void *context, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);
        static int32_t HostCallExport(void *context, const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token);
        static void HostLog(void *context, int32_t level, const uint8_t *resource, int32_t resourceLength, const uint8_t *message, int32_t messageLength);

        ResourceManager *_manager;
        ClrRuntimeConfig _config;
        HostApi _host {};
        ManagedApi _managed {};
        bool _attached       = false;
        uint64_t _generation = 0; // which attach a delegate wrapper belongs to
        std::set<std::string> _loaded;
        ValueCodec::FunctionHooks _functions;

        // JavaScript functions C# holds, by the handle it holds them under.
        std::unordered_map<uint64_t, TransferredValue> _scriptFunctions;
        uint64_t _nextScriptFunction = 0;

        // C# delegates JavaScript holds: one wrapper per delegate id.
        std::unordered_map<uint64_t, std::unique_ptr<DelegateWrapper>> _delegates;
        std::vector<uint64_t> _collectedDelegates;

        // Answers C# owes the server, by C#'s token. The server's own tokens travel in their completions.
        std::unordered_map<uint64_t, Completion> _pending;
        uint64_t _nextHostToken = 0;
    };
} // namespace Framework::Scripting::Clr
