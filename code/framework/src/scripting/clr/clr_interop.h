/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../api/native_abi.h"

#include <cstdint>

namespace Framework::Scripting::Clr {
    // The function tables the server and the managed host exchange at start (docs/scripting_csharp.md). The C#
    // mirror is Interop/HostApi.cs: change the two together and bump kInteropVersion.
    //
    // Strings are UTF-8 pointer + length. Values are ValueCodec bytes. A buffer handed back lives until the next call
    // across the same table on this thread.

    constexpr uint32_t kInteropVersion = 1;

    struct Buffer {
        const uint8_t *data = nullptr;
        int32_t length      = 0;
    };

    // Called by C#. Every function reports success as 0; on failure `out` holds the UTF-8 message.
    struct HostApi {
        uint32_t version;
        void *context;

        // The thunk of a binding member (Api::NativeCatalog), or null for an id nothing registered.
        Api::NativeThunk (*resolveNative)(uint32_t id);

        // level: 0 debug, 1 info, 2 warning, 3 error.
        void (*log)(void *context, int32_t level, const uint8_t *resource, int32_t resourceLength, const uint8_t *message, int32_t messageLength);

        // Raise an event on the server bus; `target` empty for every resource. `payload` is a value list.
        int32_t (*emitEvent)(void *context, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);

        // Call any resource's export with a value list; `out` holds the result value.
        int32_t (*callExport)(void *context, const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);
    };

    // Filled in by C#. Every function reports success as 0; on failure `out` holds the UTF-8 message.
    struct ManagedApi {
        // `scripts` is a value list of the assembly paths to load, in order.
        int32_t (*startResource)(const uint8_t *name, int32_t nameLength, const uint8_t *scripts, int32_t scriptsLength, Buffer *out);
        void (*stopResource)(const uint8_t *name, int32_t nameLength);
        void (*tick)();
        // 1 when a handler listens.
        int32_t (*wantsEvent)(int32_t scope, const uint8_t *name, int32_t nameLength);
        // 0 proceed, 1 vetoed.
        int32_t (*dispatchEvent)(int32_t scope, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength);
        // 1 when the resource registered the export.
        int32_t (*hasExport)(const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength);
        int32_t (*callExport)(const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);
        // Stop every resource; the host may start again with another HostApi.
        void (*shutdown)();
    };

    // The managed entry point: [UnmanagedCallersOnly] int Initialize(HostApi*, ManagedApi*). 0 on success.
    using InitializeFn = int32_t (*)(const HostApi *host, ManagedApi *managed);
} // namespace Framework::Scripting::Clr
