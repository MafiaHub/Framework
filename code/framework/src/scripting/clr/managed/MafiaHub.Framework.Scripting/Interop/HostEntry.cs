/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using MafiaHub.Framework.Scripting.Runtime;

namespace MafiaHub.Framework.Scripting.Interop
{
    /// <summary>
    /// Where the server enters: it calls <see cref="Initialize"/> with its function table and receives this host's
    /// (scripting/clr/clr_interop.h). No exception crosses back into the server; each entry point reports failure
    /// through its status and buffer.
    /// </summary>
    public static unsafe class HostEntry
    {
        private const uint InteropVersion = 1;

        private static HostApi s_host;
        private static bool s_attached;
        private static int s_scriptingThread = -1;
        private static IntPtr s_out;

        internal static bool IsScriptingThread => Environment.CurrentManagedThreadId == s_scriptingThread;

        internal static void EnsureScriptingThread()
        {
            if (!s_attached)
            {
                throw new InvalidOperationException("The server's C# scripting is not running.");
            }
            if (!IsScriptingThread)
            {
                throw new InvalidOperationException("The scripting API can only be used on the scripting thread; await back onto it instead of calling from a worker thread.");
            }
        }

        /// <summary>The server's entry point: a HostApi* in, a ManagedApi* filled in (scripting/clr/clr_interop.h).</summary>
        [UnmanagedCallersOnly]
        public static int Initialize(void* hostApi, void* managedApi)
        {
            var host = (HostApi*)hostApi;
            var managed = (ManagedApi*)managedApi;
            try
            {
                if (host == null || managed == null || host->Version != InteropVersion)
                {
                    return 1;
                }
                ScriptHost.Shutdown();
                Native.Reset();
                s_host = *host;
                s_scriptingThread = Environment.CurrentManagedThreadId;
                s_attached = true;

                managed->StartResource = &StartResource;
                managed->StopResource = &StopResource;
                managed->Tick = &Tick;
                managed->WantsEvent = &WantsEvent;
                managed->DispatchEvent = &DispatchEvent;
                managed->HasExport = &HasExport;
                managed->CallExport = &CallExport;
                managed->Shutdown = &Shutdown;
                return 0;
            }
            catch
            {
                return 2;
            }
        }

        internal static IntPtr ResolveNative(uint id) => s_host.ResolveNative(id);

        internal static void Log(LogLevel level, string resource, string message)
        {
            if (!s_attached || !IsScriptingThread)
            {
                Console.Error.WriteLine($"[{resource}] {message}");
                return;
            }
            byte[] owner = Encoding.UTF8.GetBytes(resource);
            byte[] text = Encoding.UTF8.GetBytes(message);
            fixed (byte* ownerPtr = owner)
            fixed (byte* textPtr = text)
            {
                s_host.Log(s_host.Context, (int)level, ownerPtr, owner.Length, textPtr, text.Length);
            }
        }

        internal static void EmitEvent(string eventName, string target, object?[] args)
        {
            EnsureScriptingThread();
            if (string.IsNullOrEmpty(eventName))
            {
                throw new ArgumentException("an event name is required", nameof(eventName));
            }
            byte[] payload = ValueCodec.EncodeList(args ?? Array.Empty<object?>());
            byte[] name = Encoding.UTF8.GetBytes(eventName);
            byte[] targetBytes = Encoding.UTF8.GetBytes(target);
            HostBuffer error;
            int status;
            fixed (byte* namePtr = name)
            fixed (byte* targetPtr = targetBytes)
            fixed (byte* payloadPtr = payload)
            {
                status = s_host.EmitEvent(s_host.Context, namePtr, name.Length, targetPtr, targetBytes.Length, payloadPtr, payload.Length, &error);
            }
            if (status != 0)
            {
                throw new InvalidOperationException($"Events.Emit('{eventName}'): {error.Text()}");
            }
        }

        internal static object? CallExport(string resourceName, string exportName, object?[] args)
        {
            EnsureScriptingThread();
            byte[] payload = ValueCodec.EncodeList(args ?? Array.Empty<object?>());
            byte[] resource = Encoding.UTF8.GetBytes(resourceName ?? string.Empty);
            byte[] name = Encoding.UTF8.GetBytes(exportName ?? string.Empty);
            HostBuffer result;
            int status;
            fixed (byte* resourcePtr = resource)
            fixed (byte* namePtr = name)
            fixed (byte* payloadPtr = payload)
            {
                status = s_host.CallExport(s_host.Context, resourcePtr, resource.Length, namePtr, name.Length, payloadPtr, payload.Length, &result);
            }
            if (status != 0)
            {
                throw new InvalidOperationException($"Exports.Call('{resourceName}', '{exportName}'): {result.Text()}");
            }
            return ValueCodec.Decode(new ReadOnlySpan<byte>(result.Data, result.Length));
        }

        private static string Utf8(byte* data, int length) => data == null || length <= 0 ? string.Empty : Encoding.UTF8.GetString(data, length);

        // What an entry point hands back to the server; valid until the next one does.
        private static void Hand(HostBuffer* output, ReadOnlySpan<byte> bytes)
        {
            if (s_out != IntPtr.Zero)
            {
                NativeMemory.Free((void*)s_out);
                s_out = IntPtr.Zero;
            }
            s_out = (IntPtr)NativeMemory.Alloc((nuint)Math.Max(bytes.Length, 1));
            bytes.CopyTo(new Span<byte>((void*)s_out, bytes.Length));
            output->Data = (byte*)s_out;
            output->Length = bytes.Length;
        }

        private static void HandText(HostBuffer* output, string text) => Hand(output, Encoding.UTF8.GetBytes(text));

        [UnmanagedCallersOnly]
        private static int StartResource(byte* name, int nameLength, byte* scripts, int scriptsLength, HostBuffer* output)
        {
            try
            {
                object?[] paths = ValueCodec.DecodeList(new ReadOnlySpan<byte>(scripts, scriptsLength));
                var list = new List<string>(paths.Length);
                foreach (object? path in paths)
                {
                    list.Add(path as string ?? string.Empty);
                }
                string? error = ScriptHost.Start(Utf8(name, nameLength), list);
                if (error != null)
                {
                    HandText(output, error);
                    return 1;
                }
                return 0;
            }
            catch (Exception error)
            {
                HandText(output, error.Message);
                return 1;
            }
        }

        [UnmanagedCallersOnly]
        private static void StopResource(byte* name, int nameLength)
        {
            try
            {
                ScriptHost.Stop(Utf8(name, nameLength));
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
            }
        }

        [UnmanagedCallersOnly]
        private static void Tick()
        {
            try
            {
                ScriptHost.Tick();
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
            }
        }

        [UnmanagedCallersOnly]
        private static int WantsEvent(int scope, byte* name, int nameLength)
        {
            try
            {
                return ScriptHost.WantsEvent((EventScope)scope, Utf8(name, nameLength)) ? 1 : 0;
            }
            catch
            {
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        private static int DispatchEvent(int scope, byte* name, int nameLength, byte* target, int targetLength, byte* payload, int payloadLength)
        {
            string eventName = Utf8(name, nameLength);
            try
            {
                object?[] args = ValueCodec.DecodeList(new ReadOnlySpan<byte>(payload, payloadLength));
                return ScriptHost.Dispatch((EventScope)scope, eventName, Utf8(target, targetLength), args) ? 0 : 1;
            }
            catch (Exception error)
            {
                Log(LogLevel.Error, "csharp", $"Event '{eventName}' could not be delivered: {error.Message}");
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        private static int HasExport(byte* resource, int resourceLength, byte* name, int nameLength)
        {
            try
            {
                return ScriptHost.HasExport(Utf8(resource, resourceLength), Utf8(name, nameLength)) ? 1 : 0;
            }
            catch
            {
                return 0;
            }
        }

        [UnmanagedCallersOnly]
        private static int CallExport(byte* resource, int resourceLength, byte* name, int nameLength, byte* payload, int payloadLength, HostBuffer* output)
        {
            try
            {
                object?[] args = ValueCodec.DecodeList(new ReadOnlySpan<byte>(payload, payloadLength));
                object? result = ScriptHost.CallExport(Utf8(resource, resourceLength), Utf8(name, nameLength), args);
                Hand(output, ValueCodec.Encode(result));
                return 0;
            }
            catch (Exception error)
            {
                Exception cause = ScriptHost.Unwrap(error);
                HandText(output, $"{cause.GetType().Name}: {cause.Message}");
                return 1;
            }
        }

        [UnmanagedCallersOnly]
        private static void Shutdown()
        {
            try
            {
                ScriptHost.Shutdown();
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
            }
            finally
            {
                s_attached = false;
                Native.Reset();
            }
        }
    }
}
