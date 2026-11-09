/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
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
        private const uint InteropVersion = 2;

        private static HostApi s_host;
        private static bool s_attached;
        private static int s_scriptingThread = -1;
        private static IntPtr s_out;

        // Which attach the server is on: handles from an earlier one name nothing now.
        internal static ulong Generation { get; private set; }

        internal static TimeSpan StopGrace => TimeSpan.FromMilliseconds(s_host.StopGraceMs);

        internal static TimeSpan UnloadCheck => TimeSpan.FromMilliseconds(s_host.UnloadCheckMs);

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
                ResetTables();
                s_host = *host;
                s_scriptingThread = Environment.CurrentManagedThreadId;
                s_attached = true;
                Generation++;

                managed->StartResource = &StartResource;
                managed->StopResource = &StopResource;
                managed->Tick = &Tick;
                managed->WantsEvent = &WantsEvent;
                managed->DispatchEvent = &DispatchEvent;
                managed->HasExport = &HasExport;
                managed->CallExport = &CallExportEntry;
                managed->InvokeFunction = &InvokeFunction;
                managed->ReleaseFunction = &ReleaseFunction;
                managed->Settle = &Settle;
                managed->Unloading = &Unloading;
                managed->Shutdown = &Shutdown;
                return 0;
            }
            catch
            {
                return 2;
            }
        }

        private static void ResetTables()
        {
            Native.Reset();
            DelegateTable.Clear();
            ServerAnswers.Clear();
            ManagedAnswers.Clear();
            while (ReleasedFunctions.Queue.TryDequeue(out _))
            {
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
            if (status != CallStatus.Ok)
            {
                throw new InvalidOperationException($"Events.Emit('{eventName}'): {error.Text()}");
            }
        }

        private delegate int OutCall(IntPtr context, HostBuffer* result, ulong* token);

        // A call into the server: its value now, or the token of an answer still to come.
        private static (object? Value, ulong Token) CallOut(string label, OutCall call)
        {
            EnsureScriptingThread();
            HostBuffer result;
            ulong token = 0;
            int status = call(s_host.Context, &result, &token);
            switch (status)
            {
                case CallStatus.Ok:
                    return (ValueCodec.Decode(result.Span), 0);
                case CallStatus.Pending:
                    return (null, token);
                default:
                    throw new InvalidOperationException($"{label}: {result.Text()}");
            }
        }

        private static (object? Value, ulong Token) CallExportOut(string resourceName, string exportName, object?[] args)
        {
            byte[] payload = ValueCodec.EncodeList(args ?? Array.Empty<object?>());
            byte[] resource = Encoding.UTF8.GetBytes(resourceName ?? string.Empty);
            byte[] name = Encoding.UTF8.GetBytes(exportName ?? string.Empty);
            return CallOut($"Exports.Call('{resourceName}', '{exportName}')", (context, result, token) =>
            {
                fixed (byte* resourcePtr = resource)
                fixed (byte* namePtr = name)
                fixed (byte* payloadPtr = payload)
                {
                    return s_host.CallExport(context, resourcePtr, resource.Length, namePtr, name.Length, payloadPtr, payload.Length, result, token);
                }
            });
        }

        private static (object? Value, ulong Token) CallFunctionOut(ScriptFunction function, object?[] args)
        {
            if (!function.IsCurrent)
            {
                throw new InvalidOperationException("this JavaScript function belongs to a C# scripting run that has stopped");
            }
            byte[] payload = ValueCodec.EncodeList(args ?? Array.Empty<object?>());
            ulong handle = function.Handle;
            GC.KeepAlive(function);
            return CallOut("Calling a JavaScript function", (context, result, token) =>
            {
                fixed (byte* payloadPtr = payload)
                {
                    return s_host.CallFunction(context, handle, payloadPtr, payload.Length, result, token);
                }
            });
        }

        private static object? Now((object? Value, ulong Token) answer, string label)
        {
            if (answer.Token != 0)
            {
                // The server settles it later and finds no one waiting.
                throw new InvalidOperationException($"{label} answered with a promise that has not settled; await the async variant instead");
            }
            return answer.Value;
        }

        private static Task<object?> Later((object? Value, ulong Token) answer) => answer.Token != 0 ? ServerAnswers.Wait(answer.Token, ScriptHost.Current) : Task.FromResult(answer.Value);

        internal static object? CallExport(string resourceName, string exportName, object?[] args) => Now(CallExportOut(resourceName, exportName, args), $"Export '{resourceName}.{exportName}'");

        internal static Task<object?> CallExportAsync(string resourceName, string exportName, object?[] args) => Later(CallExportOut(resourceName, exportName, args));

        internal static object? CallFunction(ScriptFunction function, object?[] args) => Now(CallFunctionOut(function, args), "The JavaScript function");

        internal static Task<object?> CallFunctionAsync(ScriptFunction function, object?[] args) => Later(CallFunctionOut(function, args));

        internal static void SettleHost(ulong token, int status, byte[] payload)
        {
            if (!s_attached)
            {
                return;
            }
            fixed (byte* payloadPtr = payload)
            {
                s_host.Settle(s_host.Context, token, status, payloadPtr, payload.Length);
            }
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

        private static int HandError(HostBuffer* output, Exception error)
        {
            Exception cause = ScriptHost.Unwrap(error);
            HandText(output, $"{cause.GetType().Name}: {cause.Message}");
            return CallStatus.Error;
        }

        // Runs C# code the server called and answers for it: the value, or a token settled when its task completes.
        private static int Answer(ResourceContext owner, string where, Func<object?> call, HostBuffer* output, ulong* token)
        {
            ulong pending = 0;
            byte[]? encoded = null;
            ScriptHost.RunIn(owner, () =>
            {
                object? result = call();
                if (result is Task task && !task.IsCompleted)
                {
                    pending = ManagedAnswers.Track(owner, task, where);
                    return;
                }
                encoded = ValueCodec.Encode(ValueConverter.Settled(result, where), owner);
            });
            if (pending != 0)
            {
                *token = pending;
                return CallStatus.Pending;
            }
            Hand(output, encoded!);
            return CallStatus.Ok;
        }

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
                while (ReleasedFunctions.Queue.TryDequeue(out (ulong Generation, ulong Handle) released))
                {
                    if (released.Generation == Generation)
                    {
                        s_host.ReleaseFunction(s_host.Context, released.Handle);
                    }
                }
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
        private static int CallExportEntry(byte* resource, int resourceLength, byte* name, int nameLength, byte* payload, int payloadLength, HostBuffer* output, ulong* token)
        {
            try
            {
                string resourceName = Utf8(resource, resourceLength);
                string exportName = Utf8(name, nameLength);
                (Delegate export, ResourceContext owner) = ScriptHost.FindExport(resourceName, exportName);
                object?[] args = ValueCodec.DecodeList(new ReadOnlySpan<byte>(payload, payloadLength));
                string where = $"Export '{resourceName}.{exportName}'";
                return Answer(owner, where, () => ScriptHost.Invoke(export, args, where), output, token);
            }
            catch (Exception error)
            {
                return HandError(output, error);
            }
        }

        [UnmanagedCallersOnly]
        private static int InvokeFunction(ulong id, byte* payload, int payloadLength, HostBuffer* output, ulong* token)
        {
            try
            {
                (Delegate function, ResourceContext owner) = DelegateTable.Get(id);
                object?[] args = ValueCodec.DecodeList(new ReadOnlySpan<byte>(payload, payloadLength));
                string where = $"A function of resource '{owner.Name}'";
                return Answer(owner, where, () => ScriptHost.Invoke(function, args, where), output, token);
            }
            catch (Exception error)
            {
                return HandError(output, error);
            }
        }

        [UnmanagedCallersOnly]
        private static void ReleaseFunction(ulong id)
        {
            DelegateTable.Release(id);
        }

        [UnmanagedCallersOnly]
        private static void Settle(ulong token, int status, byte* payload, int payloadLength)
        {
            try
            {
                ServerAnswers.Settle(token, status, new ReadOnlySpan<byte>(payload, payloadLength));
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
            }
        }

        [UnmanagedCallersOnly]
        private static void Unloading(HostBuffer* output)
        {
            try
            {
                var entries = new List<object?>();
                foreach ((string name, bool overdue) in ScriptHost.Unloading())
                {
                    entries.Add(new object?[] { name, overdue });
                }
                Hand(output, ValueCodec.EncodeList(entries));
            }
            catch (Exception error)
            {
                Console.Error.WriteLine(error);
                Hand(output, ValueCodec.EncodeList(Array.Empty<object?>()));
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
                ResetTables();
            }
        }
    }
}
