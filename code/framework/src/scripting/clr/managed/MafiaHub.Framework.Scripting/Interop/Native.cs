/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Generic;

namespace MafiaHub.Framework.Scripting.Interop
{
    /// <summary>
    /// Calls binding members through the server's flat convention (docs/scripting_csharp.md). Used by the generated
    /// classes; a member is named by the id it was generated with.
    /// </summary>
    public static unsafe class Native
    {
        private const int StatusOk = 0;
        private const int StatusArgument = 1;
        private const int StatusState = 2;
        private const int StatusStale = 3;

        private static readonly Dictionary<uint, IntPtr> s_thunks = new Dictionary<uint, IntPtr>();

        /// <summary>
        /// Calls member <paramref name="id"/> on the handle <paramref name="self"/>. A stale handle answers with a
        /// default value; a rejected call throws <see cref="ArgumentException"/> or <see cref="InvalidOperationException"/>.
        /// </summary>
        public static NativeValue Invoke(uint id, ulong self, NativeValue* args, int argc)
        {
            HostEntry.EnsureScriptingThread();
            var thunk = (delegate* unmanaged<ulong, NativeValue*, int, NativeValue*, int>)Resolve(id);
            NativeValue result;
            int status = thunk(self, args, argc, &result);
            switch (status)
            {
                case StatusOk:
                    return result;
                case StatusStale:
                    return default;
                case StatusArgument:
                    throw new ArgumentException(result.AsString());
                case StatusState:
                    throw new InvalidOperationException(result.AsString());
                default:
                    throw new InvalidOperationException($"binding 0x{id:X8} failed with status {status}");
            }
        }

        private static IntPtr Resolve(uint id)
        {
            if (s_thunks.TryGetValue(id, out IntPtr thunk))
            {
                return thunk;
            }
            thunk = HostEntry.ResolveNative(id);
            if (thunk == IntPtr.Zero)
            {
                throw new MissingMethodException($"The server has no binding 0x{id:X8}: this MafiaHub.Framework.Scripting does not match the server.");
            }
            s_thunks[id] = thunk;
            return thunk;
        }

        internal static void Reset() => s_thunks.Clear();
    }
}
