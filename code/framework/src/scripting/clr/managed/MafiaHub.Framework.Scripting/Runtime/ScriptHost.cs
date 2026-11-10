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
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Threading;
using System.Threading.Tasks;
using MafiaHub.Framework.Scripting.Interop;

namespace MafiaHub.Framework.Scripting.Runtime
{
    /// <summary>The C# resources the server runs: loading, lifecycle, events and exports.</summary>
    internal static class ScriptHost
    {
        private static readonly Dictionary<string, ResourceContext> s_resources = new Dictionary<string, ResourceContext>(StringComparer.Ordinal);

        // Stopped resources whose assemblies have not unloaded yet. Only a weak reference: holding the load context
        // would be the very thing keeping it loaded.
        private sealed class UnloadingEntry
        {
            public UnloadingEntry(string name, WeakReference context, IReadOnlyList<string> unfinished)
            {
                Name = name;
                Context = context;
                Unfinished = unfinished;
            }

            public string Name { get; }
            public WeakReference Context { get; }
            public IReadOnlyList<string> Unfinished { get; }
            public long StoppedAt { get; } = Stopwatch.GetTimestamp();
            public bool Overdue { get; set; }
        }

        private static readonly List<UnloadingEntry> s_unloading = new List<UnloadingEntry>();
        private static long s_nextUnloadCheck;

        // While something is unloading, how often a collection checks; once all that is left is overdue, once per
        // unload check period (at least a second), so a lingering resource does not cost a collection every tick.
        private static readonly TimeSpan UnloadCheckInterval = TimeSpan.FromMilliseconds(100);
        private static TimeSpan OverdueCheckInterval => HostEntry.UnloadCheck > TimeSpan.FromSeconds(1) ? HostEntry.UnloadCheck : TimeSpan.FromSeconds(1);

        public static ResourceContext? Current => SynchronizationContext.Current is ResourceSynchronizationContext sync ? sync.Resource : null;

        public static ResourceContext RequireCurrent(string api)
        {
            ResourceContext? current = Current;
            if (current is null || current.Stopped)
            {
                throw new InvalidOperationException($"{api} must be called from a running resource's script, on the scripting thread");
            }
            return current;
        }

        public static void Post(ResourceContext resource, SendOrPostCallback callback, object? state) => PostQueue.Items.Enqueue(new PostedWork(resource, callback, state));

        public static void RunIn(ResourceContext resource, Action action)
        {
            SynchronizationContext? previous = SynchronizationContext.Current;
            SynchronizationContext.SetSynchronizationContext(resource.Sync);
            try
            {
                action();
            }
            finally
            {
                SynchronizationContext.SetSynchronizationContext(previous);
            }
        }

        /// <summary>Loads a resource and runs its scripts' OnStart. Returns the error, or null.</summary>
        public static string? Start(string name, IReadOnlyList<string> paths)
        {
            if (s_resources.ContainsKey(name))
            {
                return $"resource '{name}' is already loaded";
            }
            if (paths.Count == 0)
            {
                return "a C# resource lists its assemblies in serverScripts";
            }

            var resource = new ResourceContext(name, Path.GetDirectoryName(paths[0]) ?? ".");
            s_resources[name] = resource;
            try
            {
                foreach (string path in paths)
                {
                    if (!path.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
                    {
                        throw new InvalidOperationException($"'{Path.GetFileName(path)}' is not an assembly; a C# resource's serverScripts are .dll files");
                    }
                    Assembly assembly = resource.LoadContext.LoadFromFile(path);
                    foreach (Type type in assembly.GetTypes().Where(t => !t.IsAbstract && typeof(ServerScript).IsAssignableFrom(t)))
                    {
                        var script = (ServerScript)(Activator.CreateInstance(type) ?? throw new InvalidOperationException($"{type.FullName} could not be created"));
                        script.ResourceName = name;
                        script.Context = resource;
                        resource.Scripts.Add(script);
                    }
                }
                if (resource.Scripts.Count == 0)
                {
                    throw new InvalidOperationException("its assemblies define no ServerScript");
                }
                RunIn(resource, () =>
                {
                    foreach (ServerScript script in resource.Scripts)
                    {
                        script.OnStart();
                    }
                });
                return null;
            }
            catch (Exception error)
            {
                Exception cause = Unwrap(error);
                Log.Write(LogLevel.Error, name, cause.ToString());
                End(resource);
                return $"{cause.GetType().Name}: {cause.Message}";
            }
        }

        /// <summary>
        /// Stops a resource: cancels its Stopping token, runs its OnStop, disposes what it tracked, waits for its
        /// background work, then drops everything it registered and unloads its assemblies.
        /// </summary>
        public static void Stop(string name)
        {
            if (!s_resources.TryGetValue(name, out ResourceContext? resource))
            {
                return;
            }
            RunIn(resource, () =>
            {
                Guard(name, "Cancelling Stopping", resource.Stopping.Cancel);
                foreach (ServerScript script in resource.Scripts)
                {
                    Guard(name, "OnStop", script.OnStop);
                }
            });
            End(resource);
        }

        public static void Shutdown()
        {
            foreach (string name in s_resources.Keys.ToArray())
            {
                Stop(name);
            }
            PostQueue.Items.Clear();
        }

        public static void Tick()
        {
            // Only what was queued before this tick: work that queues more runs next tick.
            int pending = PostQueue.Items.Count;
            for (int i = 0; i < pending && PostQueue.Items.TryDequeue(out PostedWork work); ++i)
            {
                if (work.Resource.Stopped)
                {
                    continue;
                }
                RunIn(work.Resource, () => Guard(work.Resource.Name, "A continuation", () => work.Callback(work.State)));
            }

            long now = Stopwatch.GetTimestamp();
            foreach (ResourceContext resource in s_resources.Values.ToArray())
            {
                if (resource.Stopped)
                {
                    continue;
                }
                RunIn(resource, () =>
                {
                    foreach (ScriptTimer timer in resource.Timers.ToArray())
                    {
                        if (timer.Disposed || now < timer.DueTimestamp || resource.Stopped)
                        {
                            continue;
                        }
                        if (timer.IntervalTicks > 0)
                        {
                            timer.DueTimestamp = Math.Max(timer.DueTimestamp + timer.IntervalTicks, now + 1);
                        }
                        else
                        {
                            timer.Dispose();
                        }
                        Guard(resource.Name, "A timer", timer.Action);
                    }
                    foreach (ServerScript script in resource.Scripts)
                    {
                        Guard(resource.Name, "OnTick", script.OnTick);
                    }
                });
            }

            CheckUnloading(now);
        }

        /// <summary>Stopped resources whose assemblies are still loaded, and whether they are overdue.</summary>
        public static IEnumerable<(string Name, bool Overdue)> Unloading() => s_unloading.Select(entry => (entry.Name, entry.Overdue)).ToArray();

        public static bool WantsEvent(EventScope scope, string eventName)
        {
            foreach (ResourceContext resource in s_resources.Values)
            {
                if (resource.Handlers(scope).TryGetValue(eventName, out List<EventHandlerEntry>? handlers) && handlers.Count > 0)
                {
                    return true;
                }
            }
            return false;
        }

        /// <summary>Calls every handler of the event; false when one returned false.</summary>
        public static bool Dispatch(EventScope scope, string eventName, string target, object?[] args)
        {
            bool proceed = true;
            foreach (ResourceContext resource in s_resources.Values.ToArray())
            {
                if ((target.Length > 0 && resource.Name != target) || resource.Stopped)
                {
                    continue;
                }
                if (!resource.Handlers(scope).TryGetValue(eventName, out List<EventHandlerEntry>? handlers))
                {
                    continue;
                }
                foreach (EventHandlerEntry entry in handlers.ToArray())
                {
                    if (entry.Once)
                    {
                        handlers.Remove(entry);
                    }
                    RunIn(resource, () =>
                    {
                        try
                        {
                            object? result = Invoke(entry.Handler, args, $"Event '{eventName}' handler");
                            if (result is false)
                            {
                                proceed = false;
                            }
                            else if (result is Task task)
                            {
                                Observe(resource, task, $"Event '{eventName}' handler");
                            }
                        }
                        catch (Exception error)
                        {
                            Log.Write(LogLevel.Error, resource.Name, $"Event '{eventName}' handler failed: {Unwrap(error)}");
                        }
                    });
                }
            }
            return proceed;
        }

        public static bool HasExport(string resourceName, string exportName) => s_resources.TryGetValue(resourceName, out ResourceContext? resource) && !resource.Stopped && resource.Exports.ContainsKey(exportName);

        public static (Delegate Export, ResourceContext Owner) FindExport(string resourceName, string exportName)
        {
            if (!s_resources.TryGetValue(resourceName, out ResourceContext? resource) || resource.Stopped || !resource.Exports.TryGetValue(exportName, out Delegate? export))
            {
                throw new InvalidOperationException($"export '{exportName}' not found in resource '{resourceName}'");
            }
            return (export, resource);
        }

        public static object? Invoke(Delegate handler, object?[] args, string where)
        {
            object?[] arguments = ValueConverter.Arguments(handler.Method, args, where);
            try
            {
                return handler.DynamicInvoke(arguments);
            }
            catch (TargetInvocationException error) when (error.InnerException != null)
            {
                ExceptionDispatchInfo.Capture(error.InnerException).Throw();
                throw;
            }
        }

        public static Exception Unwrap(Exception error)
        {
            while (error is TargetInvocationException or AggregateException && error.InnerException != null)
            {
                error = error.InnerException;
            }
            return error;
        }

        // Runs resource code, reporting a failure instead of letting it take the rest down.
        private static void Guard(string resource, string what, Action action)
        {
            try
            {
                action();
            }
            catch (Exception error)
            {
                Log.Write(LogLevel.Error, resource, $"{what} failed: {Unwrap(error)}");
            }
        }

        // An async handler's failure is reported when it happens, on the scripting thread.
        private static void Observe(ResourceContext resource, Task task, string where)
        {
            task.ContinueWith(completed =>
            {
                if (completed.Exception != null)
                {
                    Log.Write(LogLevel.Error, resource.Name, $"{where} failed: {Unwrap(completed.Exception)}");
                }
            }, CancellationToken.None, TaskContinuationOptions.OnlyOnFaulted, TaskScheduler.FromCurrentSynchronizationContext());
        }

        // Everything after OnStop, for a stop and for a start that failed half way.
        private static void End(ResourceContext resource)
        {
            string name = resource.Name;
            Guard(name, "Cancelling Stopping", resource.Stopping.Cancel);

            // What it tracked, newest first, while its code may still run.
            RunIn(resource, () =>
            {
                for (int i = resource.Tracked.Count - 1; i >= 0; --i)
                {
                    object tracked = resource.Tracked[i];
                    Guard(name, $"Disposing {tracked.GetType().Name}", () =>
                    {
                        if (tracked is IAsyncDisposable asyncDisposable)
                        {
                            ValueTask disposing = asyncDisposable.DisposeAsync();
                            if (!disposing.IsCompleted)
                            {
                                resource.Background.Add(($"DisposeAsync of {tracked.GetType().Name}", disposing.AsTask()));
                            }
                        }
                        else if (tracked is IDisposable disposable)
                        {
                            disposable.Dispose();
                        }
                    });
                }
            });

            // Its background work has the grace period to see the cancellation and end.
            Task[] running = resource.Background.Select(work => work.Task).Where(task => !task.IsCompleted).ToArray();
            if (running.Length > 0)
            {
                try
                {
                    Task.WaitAll(running, HostEntry.StopGrace);
                }
                catch (AggregateException)
                {
                    // Failures and cancellations are how background work ends; only what still runs matters here.
                }
            }
            string[] unfinished = resource.Background.Where(work => !work.Task.IsCompleted).Select(work => work.Name).ToArray();
            if (unfinished.Length > 0)
            {
                Log.Write(LogLevel.Warning, name, $"Background work still running after the stop: {string.Join(", ", unfinished)}. End it when Stopping is cancelled.");
            }

            ManagedAnswers.FailResource(resource);
            ServerAnswers.DropResource(resource);
            DelegateTable.ReleaseResource(resource);

            resource.Stopped = true;
            resource.GlobalHandlers.Clear();
            resource.ClientHandlers.Clear();
            resource.Exports.Clear();
            resource.Scripts.Clear();
            resource.Tracked.Clear();
            resource.Background.Clear();
            foreach (ScriptTimer timer in resource.Timers.ToArray())
            {
                timer.Dispose();
            }
            s_resources.Remove(name);

            var context = new WeakReference(resource.LoadContext, trackResurrection: true);
            resource.LoadContext.Unload();
            s_unloading.Add(new UnloadingEntry(name, context, unfinished));
            s_nextUnloadCheck = 0;
        }

        // Collects until each stopped resource's assemblies are gone, and reports one that outlives unloadCheckMs.
        private static void CheckUnloading(long now)
        {
            if (s_unloading.Count == 0 || now < s_nextUnloadCheck)
            {
                return;
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();

            foreach (UnloadingEntry entry in s_unloading.ToArray())
            {
                if (!entry.Context.IsAlive)
                {
                    s_unloading.Remove(entry);
                    if (entry.Overdue)
                    {
                        Log.Write(LogLevel.Info, entry.Name, "Its assemblies have unloaded after all.");
                    }
                    continue;
                }
                if (!entry.Overdue && Stopwatch.GetElapsedTime(entry.StoppedAt) >= HostEntry.UnloadCheck)
                {
                    entry.Overdue = true;
                    string unfinished = entry.Unfinished.Count > 0 ? $" Background work that did not end: {string.Join(", ", entry.Unfinished)}." : string.Empty;
                    Log.Write(LogLevel.Warning, entry.Name,
                        $"Stopped {HostEntry.UnloadCheck.TotalSeconds:0.#}s ago but its code is still loaded: something it started is still running or referenced (a thread, a timer, a socket, a static event subscription).{unfinished} End it in OnStop, when Stopping is cancelled, or by registering it with Track().");
                }
            }

            bool waiting = s_unloading.Any(entry => !entry.Overdue);
            s_nextUnloadCheck = now + (long)((waiting ? UnloadCheckInterval : OverdueCheckInterval).TotalSeconds * Stopwatch.Frequency);
        }
    }
}
