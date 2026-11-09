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

#include <set>
#include <string>
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
        Utils::Result<TransferredValue, std::string> CallExport(const std::string &resourceName, const std::string &exportName, const std::vector<TransferredValue> &args) override;

        // Whether this runtime has started .NET and attached to the managed host.
        bool IsAttached() const {
            return _attached;
        }

        // The hostfxr library this configuration would start, or an error saying where it looked.
        static Utils::Result<std::string, std::string> LocateHostFxr(const std::string &dotnetRoot);

      private:
        Utils::Result<void, std::string> Attach();

        static int32_t HostEmitEvent(void *context, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);
        static int32_t HostCallExport(void *context, const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);
        static void HostLog(void *context, int32_t level, const uint8_t *resource, int32_t resourceLength, const uint8_t *message, int32_t messageLength);

        ResourceManager *_manager;
        ClrRuntimeConfig _config;
        HostApi _host {};
        ManagedApi _managed {};
        bool _attached = false;
        std::set<std::string> _loaded;
    };
} // namespace Framework::Scripting::Clr
