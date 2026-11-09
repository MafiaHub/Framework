/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace MafiaHub.Framework.Scripting.Interop
{
    // Mirrors scripting/clr/clr_interop.h; change the two together and bump InteropVersion.

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct HostBuffer
    {
        public byte* Data;
        public int Length;

        public string Text() => Data == null || Length <= 0 ? string.Empty : Encoding.UTF8.GetString(Data, Length);
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct HostApi
    {
        public uint Version;
        public IntPtr Context;
        public delegate* unmanaged<uint, IntPtr> ResolveNative;
        public delegate* unmanaged<IntPtr, int, byte*, int, byte*, int, void> Log;
        public delegate* unmanaged<IntPtr, byte*, int, byte*, int, byte*, int, HostBuffer*, int> EmitEvent;
        public delegate* unmanaged<IntPtr, byte*, int, byte*, int, byte*, int, HostBuffer*, int> CallExport;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct ManagedApi
    {
        public delegate* unmanaged<byte*, int, byte*, int, HostBuffer*, int> StartResource;
        public delegate* unmanaged<byte*, int, void> StopResource;
        public delegate* unmanaged<void> Tick;
        public delegate* unmanaged<int, byte*, int, int> WantsEvent;
        public delegate* unmanaged<int, byte*, int, byte*, int, byte*, int, int> DispatchEvent;
        public delegate* unmanaged<byte*, int, byte*, int, int> HasExport;
        public delegate* unmanaged<byte*, int, byte*, int, byte*, int, HostBuffer*, int> CallExport;
        public delegate* unmanaged<void> Shutdown;
    }
}
