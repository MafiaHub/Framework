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
    //
    // A call answers with a CallStatus. Pending hands out a token; the side that answered settles it later through the
    // other side's settle(), from the scripting thread.

    constexpr uint32_t kInteropVersion = 2;

    enum CallStatus : int32_t {
        kCallOk      = 0, // `out` holds the value
        kCallError   = 1, // `out` holds the UTF-8 message
        kCallPending = 2, // `token` names the answer still to come
    };

    struct Buffer {
        const uint8_t *data = nullptr;
        int32_t length      = 0;
    };

    // Called by C#.
    struct HostApi {
        uint32_t version;
        void *context;

        // How long a stopping resource's background work may take to end, and how long after a stop its assemblies
        // may stay loaded before the host reports it.
        uint32_t stopGraceMs;
        uint32_t unloadCheckMs;

        // The thunk of a binding member (Api::NativeCatalog), or null for an id nothing registered.
        Api::NativeThunk (*resolveNative)(uint32_t id);

        // level: 0 debug, 1 info, 2 warning, 3 error.
        void (*log)(void *context, int32_t level, const uint8_t *resource, int32_t resourceLength, const uint8_t *message, int32_t messageLength);

        // Raise an event on the server bus; `target` empty for every resource. `payload` is a value list. A CallStatus.
        int32_t (*emitEvent)(void *context, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength, Buffer *out);

        // Call any resource's export with a value list. A CallStatus.
        int32_t (*callExport)(void *context, const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token);

        // Call a JavaScript function C# holds (codec tag 14). A CallStatus.
        int32_t (*callFunction)(void *context, uint64_t handle, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token);

        // C# no longer holds this JavaScript function.
        void (*releaseFunction)(void *context, uint64_t handle);

        // Settle what C# answered kCallPending for: a value, or a message for kCallError.
        void (*settle)(void *context, uint64_t token, int32_t status, const uint8_t *payload, int32_t payloadLength);
    };

    // Filled in by C#.
    struct ManagedApi {
        // `scripts` is a value list of the assembly paths to load, in order. 0, or 1 with the message in `out`.
        int32_t (*startResource)(const uint8_t *name, int32_t nameLength, const uint8_t *scripts, int32_t scriptsLength, Buffer *out);
        void (*stopResource)(const uint8_t *name, int32_t nameLength);
        void (*tick)();
        // 1 when a handler listens.
        int32_t (*wantsEvent)(int32_t scope, const uint8_t *name, int32_t nameLength);
        // 0 proceed, 1 vetoed.
        int32_t (*dispatchEvent)(int32_t scope, const uint8_t *name, int32_t nameLength, const uint8_t *target, int32_t targetLength, const uint8_t *payload, int32_t payloadLength);
        // 1 when the resource registered the export.
        int32_t (*hasExport)(const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength);
        // A CallStatus.
        int32_t (*callExport)(const uint8_t *resource, int32_t resourceLength, const uint8_t *name, int32_t nameLength, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token);
        // Call a C# delegate JavaScript holds (codec tag 15). A CallStatus.
        int32_t (*invokeFunction)(uint64_t id, const uint8_t *payload, int32_t payloadLength, Buffer *out, uint64_t *token);
        // JavaScript no longer holds this delegate.
        void (*releaseFunction)(uint64_t id);
        // Settle what the server answered kCallPending for.
        void (*settle)(uint64_t token, int32_t status, const uint8_t *payload, int32_t payloadLength);
        // Stopped resources whose assemblies are still loaded: a value list of [name, overdue] pairs, overdue once
        // unloadCheckMs has passed.
        void (*unloading)(Buffer *out);
        // Stop every resource; the host may start again with another HostApi.
        void (*shutdown)();
    };

    // The managed entry point: [UnmanagedCallersOnly] int Initialize(HostApi*, ManagedApi*). 0 on success.
    using InitializeFn = int32_t (*)(const HostApi *host, ManagedApi *managed);
} // namespace Framework::Scripting::Clr
