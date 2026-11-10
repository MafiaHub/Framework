/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using MafiaHub.Framework.Scripting.Interop;

namespace MafiaHub.Framework.Scripting.Runtime
{
    /// <summary>
    /// C# delegates JavaScript holds, by the id they crossed under. A delegate belongs to the resource that sent it and
    /// goes with it when that resource stops; JavaScript calling it afterwards gets an error.
    /// </summary>
    internal static class DelegateTable
    {
        private sealed record Entry(Delegate Function, ResourceContext Owner);

        private static readonly Dictionary<ulong, Entry> s_byId = new Dictionary<ulong, Entry>();
        private static readonly Dictionary<Delegate, ulong> s_byDelegate = new Dictionary<Delegate, ulong>();
        private static ulong s_next;

        public static ulong Register(Delegate function, ResourceContext owner)
        {
            if (s_byDelegate.TryGetValue(function, out ulong existing) && s_byId.TryGetValue(existing, out Entry? entry) && entry.Owner == owner)
            {
                return existing;
            }
            ulong id = ++s_next;
            s_byId[id] = new Entry(function, owner);
            s_byDelegate[function] = id;
            return id;
        }

        public static (Delegate Function, ResourceContext Owner) Get(ulong id)
        {
            if (!s_byId.TryGetValue(id, out Entry? entry) || entry.Owner.Stopped)
            {
                throw new InvalidOperationException("this C# function's resource has stopped");
            }
            return (entry.Function, entry.Owner);
        }

        public static void Release(ulong id)
        {
            if (s_byId.Remove(id, out Entry? entry) && s_byDelegate.TryGetValue(entry.Function, out ulong current) && current == id)
            {
                s_byDelegate.Remove(entry.Function);
            }
        }

        public static void ReleaseResource(ResourceContext owner)
        {
            foreach (ulong id in s_byId.Where(pair => pair.Value.Owner == owner).Select(pair => pair.Key).ToArray())
            {
                Release(id);
            }
        }

        public static void Clear()
        {
            s_byId.Clear();
            s_byDelegate.Clear();
        }
    }

    /// <summary>Answers the server still owes C#: a promise a JavaScript call returned, by the server's token.</summary>
    internal static class ServerAnswers
    {
        private sealed record Waiting(TaskCompletionSource<object?> Source, ResourceContext? Owner);

        private static readonly Dictionary<ulong, Waiting> s_waiting = new Dictionary<ulong, Waiting>();

        public static Task<object?> Wait(ulong token, ResourceContext? owner)
        {
            var source = new TaskCompletionSource<object?>();
            s_waiting[token] = new Waiting(source, owner);
            return source.Task;
        }

        public static void Settle(ulong token, int status, ReadOnlySpan<byte> payload)
        {
            if (!s_waiting.Remove(token, out Waiting? waiting))
            {
                return;
            }
            if (status != CallStatus.Ok)
            {
                waiting.Source.TrySetException(new InvalidOperationException(System.Text.Encoding.UTF8.GetString(payload)));
                return;
            }
            try
            {
                waiting.Source.TrySetResult(ValueCodec.Decode(payload));
            }
            catch (Exception error)
            {
                waiting.Source.TrySetException(error);
            }
        }

        // A stopping resource hears nothing more: its continuations would be dropped anyway.
        public static void DropResource(ResourceContext owner)
        {
            foreach (ulong token in s_waiting.Where(pair => pair.Value.Owner == owner).Select(pair => pair.Key).ToArray())
            {
                s_waiting.Remove(token);
            }
        }

        public static void Clear() => s_waiting.Clear();
    }

    /// <summary>Answers C# still owes the server: a task an export or a delegate returned, by C#'s token.</summary>
    internal static class ManagedAnswers
    {
        private static readonly Dictionary<ulong, ResourceContext> s_owing = new Dictionary<ulong, ResourceContext>();
        private static ulong s_next;

        /// <summary>Settles the server's token when the task completes. Call on the owner's scripting context.</summary>
        public static ulong Track(ResourceContext owner, Task task, string where)
        {
            ulong token = ++s_next;
            s_owing[token] = owner;
            task.ContinueWith(completed =>
            {
                if (!s_owing.Remove(token))
                {
                    return;
                }
                try
                {
                    object? result = ValueConverter.Settled(completed, where);
                    HostEntry.SettleHost(token, CallStatus.Ok, ValueCodec.Encode(result, owner));
                }
                catch (Exception error)
                {
                    Exception cause = ScriptHost.Unwrap(error);
                    HostEntry.SettleHost(token, CallStatus.Error, System.Text.Encoding.UTF8.GetBytes($"{cause.GetType().Name}: {cause.Message}"));
                }
            }, CancellationToken.None, TaskContinuationOptions.None, TaskScheduler.FromCurrentSynchronizationContext());
            return token;
        }

        // Whoever waits on a stopping resource is told, instead of waiting forever.
        public static void FailResource(ResourceContext owner)
        {
            foreach (ulong token in s_owing.Where(pair => pair.Value == owner).Select(pair => pair.Key).ToArray())
            {
                s_owing.Remove(token);
                HostEntry.SettleHost(token, CallStatus.Error, System.Text.Encoding.UTF8.GetBytes($"resource '{owner.Name}' stopped before answering"));
            }
        }

        public static void Clear() => s_owing.Clear();
    }

    /// <summary>JavaScript functions C# let go of, released on the scripting thread at the next tick.</summary>
    internal static class ReleasedFunctions
    {
        public static readonly ConcurrentQueue<(ulong Generation, ulong Handle)> Queue = new ConcurrentQueue<(ulong, ulong)>();
    }
}
