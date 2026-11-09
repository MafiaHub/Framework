/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using MafiaHub.Framework.Scripting.Interop;
using MafiaHub.Framework.Scripting.Runtime;

namespace MafiaHub.Framework.Scripting
{
    /// <summary>
    /// A server script. The server creates one of every non-abstract subclass in a resource's assemblies when the
    /// resource starts. Everything a script does runs on the scripting thread; <c>await</c> resumes there too.
    /// </summary>
    /// <remarks>
    /// Stopping a resource cannot kill what it started: .NET has no way to end another thread or find a socket. A stop
    /// cancels <see cref="Stopping"/>, runs <see cref="OnStop"/>, disposes what was passed to <see cref="Track{T}"/>,
    /// waits briefly for <see cref="Background"/> work, and drops the resource's timers, handlers, exports and pending
    /// continuations. Anything left running keeps the resource's assemblies loaded, and the server reports it.
    /// </remarks>
    public abstract class ServerScript
    {
        /// <summary>The resource this script belongs to.</summary>
        public string ResourceName { get; internal set; } = string.Empty;

        internal ResourceContext Context { get; set; } = null!;

        /// <summary>Cancelled when the resource starts stopping, before <see cref="OnStop"/>.</summary>
        protected CancellationToken Stopping => Context.Stopping.Token;

        /// <summary>Disposes <paramref name="disposable"/> when the resource stops, after <see cref="OnStop"/>; newest first.</summary>
        protected T Track<T>(T disposable) where T : IDisposable
        {
            ArgumentNullException.ThrowIfNull(disposable);
            Context.Tracked.Add(disposable);
            return disposable;
        }

        /// <summary>Disposes <paramref name="disposable"/> when the resource stops; the stop waits for it like background work.</summary>
        protected T TrackAsync<T>(T disposable) where T : IAsyncDisposable
        {
            ArgumentNullException.ThrowIfNull(disposable);
            Context.Tracked.Add(disposable);
            return disposable;
        }

        /// <summary>
        /// Runs <paramref name="work"/> on the thread pool with the <see cref="Stopping"/> token. The stop waits for it to
        /// end, briefly, and names it if it does not. It cannot use the scripting API directly: post back with
        /// <c>await</c> from the script instead.
        /// </summary>
        protected Task Background(string name, Func<CancellationToken, Task> work)
        {
            ArgumentNullException.ThrowIfNull(work);
            ResourceContext context = Context;
            CancellationToken token = context.Stopping.Token;
            Task task = Task.Run(() => work(token), CancellationToken.None);
            context.Background.Add((name, task));
            task.ContinueWith(completed =>
            {
                Exception? error = completed.Exception?.InnerException;
                if (error != null && !(error is OperationCanceledException && token.IsCancellationRequested))
                {
                    Log.Write(LogLevel.Error, context.Name, $"Background work '{name}' failed: {error}");
                }
            }, CancellationToken.None, TaskContinuationOptions.OnlyOnFaulted, SynchronizationContext.Current != null ? TaskScheduler.FromCurrentSynchronizationContext() : TaskScheduler.Default);
            return task;
        }

        /// <summary>The resource is starting: register event handlers and exports here.</summary>
        protected internal virtual void OnStart()
        {
        }

        /// <summary>The resource is stopping. Its handlers and exports are removed afterwards.</summary>
        protected internal virtual void OnStop()
        {
        }

        /// <summary>Once per server tick.</summary>
        protected internal virtual void OnTick()
        {
        }
    }

    /// <summary>Which handlers an event reaches.</summary>
    public enum EventScope
    {
        /// <summary>Events raised by the server and its resources.</summary>
        Global = 0,

        /// <summary>Events a client sent. Kept apart so a client can never raise a server event.</summary>
        Client = 1,
    }

    /// <summary>
    /// The server's event bus, shared with JavaScript resources: an event emitted here reaches their
    /// <c>Events.on</c> handlers, and theirs reach handlers registered here.
    /// </summary>
    /// <remarks>
    /// A handler is any delegate; its parameters are filled from the event's arguments (see <see cref="Exports"/>
    /// for how values convert). A handler returning <c>false</c> vetoes events the server raises synchronously.
    /// </remarks>
    public static class Events
    {
        /// <summary>Handles an event until the returned subscription is disposed or the resource stops.</summary>
        public static IDisposable On(string eventName, Delegate handler) => Add(EventScope.Global, eventName, handler, false);

        /// <summary>Handles the next occurrence of an event only.</summary>
        public static IDisposable Once(string eventName, Delegate handler) => Add(EventScope.Global, eventName, handler, true);

        /// <summary>Handles an event a client sent. The first argument is the sending <see cref="Player"/>.</summary>
        public static IDisposable OnClient(string eventName, Delegate handler) => Add(EventScope.Client, eventName, handler, false);

        /// <summary>Raises an event for every resource, in every language.</summary>
        public static void Emit(string eventName, params object?[] args) => HostEntry.EmitEvent(eventName, string.Empty, args);

        /// <summary>Raises an event for one resource's handlers only.</summary>
        public static void EmitTo(string resourceName, string eventName, params object?[] args)
        {
            if (string.IsNullOrEmpty(resourceName))
            {
                throw new ArgumentException("EmitTo needs a resource name", nameof(resourceName));
            }
            HostEntry.EmitEvent(eventName, resourceName, args);
        }

        private static IDisposable Add(EventScope scope, string eventName, Delegate handler, bool once)
        {
            if (string.IsNullOrEmpty(eventName))
            {
                throw new ArgumentException("an event name is required", nameof(eventName));
            }
            ArgumentNullException.ThrowIfNull(handler);
            ResourceContext resource = ScriptHost.RequireCurrent("Events.On");
            Dictionary<string, List<EventHandlerEntry>> table = resource.Handlers(scope);
            if (!table.TryGetValue(eventName, out List<EventHandlerEntry>? handlers))
            {
                handlers = new List<EventHandlerEntry>();
                table[eventName] = handlers;
            }
            var entry = new EventHandlerEntry(eventName, handler, once);
            handlers.Add(entry);
            return new Subscription(() => handlers.Remove(entry));
        }

        private sealed class Subscription : IDisposable
        {
            private Action? _remove;

            public Subscription(Action remove)
            {
                _remove = remove;
            }

            public void Dispose()
            {
                _remove?.Invoke();
                _remove = null;
            }
        }
    }

    /// <summary>
    /// Functions resources offer each other, in any language. A JavaScript resource reaches an export registered
    /// here with <c>Exports.get(resource, name)</c>, and this calls theirs.
    /// </summary>
    /// <remarks>
    /// Values are copied between resources. A number arrives as <see cref="double"/>, an array as <c>object?[]</c>,
    /// an object as <c>Dictionary&lt;string, object?&gt;</c>; a parameter of another type is converted to it.
    /// A function crosses as a <see cref="ScriptFunction"/> (or the original delegate, when it came from C#). An
    /// export may answer later: <see cref="CallAsync(string, string, object[])"/> awaits it, and one registered here may
    /// return a task.
    /// </remarks>
    public static class Exports
    {
        /// <summary>Offers a function to other resources under <paramref name="exportName"/>.</summary>
        public static void Register(string exportName, Delegate function)
        {
            if (string.IsNullOrEmpty(exportName))
            {
                throw new ArgumentException("an export name is required", nameof(exportName));
            }
            ArgumentNullException.ThrowIfNull(function);
            ScriptHost.RequireCurrent("Exports.Register").Exports[exportName] = function;
        }

        /// <summary>Calls another resource's export and returns what it returned.</summary>
        public static object? Call(string resourceName, string exportName, params object?[] args) => HostEntry.CallExport(resourceName, exportName, args);

        /// <summary>Calls another resource's export and converts what it returned.</summary>
        public static T? Call<T>(string resourceName, string exportName, params object?[] args)
        {
            object? result = HostEntry.CallExport(resourceName, exportName, args);
            return (T?)ValueConverter.To(result, typeof(T), $"Export '{resourceName}.{exportName}' result");
        }

        /// <summary>Calls another resource's export; the task completes when a promise or task it returned settles.</summary>
        public static Task<object?> CallAsync(string resourceName, string exportName, params object?[] args) => HostEntry.CallExportAsync(resourceName, exportName, args);

        /// <summary>Calls another resource's export and converts what it settles with.</summary>
        public static async Task<T?> CallAsync<T>(string resourceName, string exportName, params object?[] args)
        {
            object? result = await HostEntry.CallExportAsync(resourceName, exportName, args);
            return (T?)ValueConverter.To(result, typeof(T), $"Export '{resourceName}.{exportName}' result");
        }
    }

    /// <summary>
    /// Timers that run on the scripting thread, from the server tick, and end with the resource that made them. The
    /// counterpart of <c>setTimeout</c> and <c>setInterval</c>.
    /// </summary>
    public static class Timers
    {
        /// <summary>Runs <paramref name="action"/> once after <paramref name="delay"/>. Dispose the result to cancel it.</summary>
        public static IDisposable After(TimeSpan delay, Action action) => Add(delay, TimeSpan.Zero, action, "Timers.After");

        /// <summary>Runs <paramref name="action"/> every <paramref name="interval"/>, at most once per tick. Dispose the result to stop it.</summary>
        public static IDisposable Every(TimeSpan interval, Action action)
        {
            if (interval <= TimeSpan.Zero)
            {
                throw new ArgumentOutOfRangeException(nameof(interval), "an interval must be positive");
            }
            return Add(interval, interval, action, "Timers.Every");
        }

        private static IDisposable Add(TimeSpan delay, TimeSpan interval, Action action, string api)
        {
            ArgumentNullException.ThrowIfNull(action);
            ResourceContext resource = ScriptHost.RequireCurrent(api);
            long due = Stopwatch.GetTimestamp() + Ticks(delay);
            var timer = new ScriptTimer(resource, due, Ticks(interval), action);
            resource.Timers.Add(timer);
            return timer;
        }

        private static long Ticks(TimeSpan span) => (long)(Math.Max(span.TotalSeconds, 0) * Stopwatch.Frequency);
    }

    /// <summary>Log levels of <see cref="Log"/>.</summary>
    public enum LogLevel
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
    }

    /// <summary>Writes to the server log, under the calling resource's name.</summary>
    public static class Log
    {
        public static void Debug(string message) => Write(LogLevel.Debug, null, message);
        public static void Info(string message) => Write(LogLevel.Info, null, message);
        public static void Warning(string message) => Write(LogLevel.Warning, null, message);
        public static void Error(string message) => Write(LogLevel.Error, null, message);

        internal static void Write(LogLevel level, string? resource, string message) => HostEntry.Log(level, resource ?? ScriptHost.Current?.Name ?? "csharp", message);
    }
}
