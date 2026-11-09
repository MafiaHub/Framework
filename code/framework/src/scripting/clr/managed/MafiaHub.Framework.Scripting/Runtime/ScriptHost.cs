/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Threading;
using System.Threading.Tasks;

namespace MafiaHub.Framework.Scripting.Runtime
{
    /// <summary>The C# resources the server runs: loading, lifecycle, events and exports.</summary>
    internal static class ScriptHost
    {
        private static readonly Dictionary<string, ResourceContext> s_resources = new Dictionary<string, ResourceContext>(StringComparer.Ordinal);

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
                Unload(resource);
                Exception cause = Unwrap(error);
                Log.Write(LogLevel.Error, name, cause.ToString());
                return $"{cause.GetType().Name}: {cause.Message}";
            }
        }

        /// <summary>Runs the resource's OnStop, then drops its handlers and exports and unloads its assemblies.</summary>
        public static void Stop(string name)
        {
            if (!s_resources.TryGetValue(name, out ResourceContext? resource))
            {
                return;
            }
            RunIn(resource, () =>
            {
                foreach (ServerScript script in resource.Scripts)
                {
                    try
                    {
                        script.OnStop();
                    }
                    catch (Exception error)
                    {
                        Log.Write(LogLevel.Error, name, $"OnStop failed: {Unwrap(error)}");
                    }
                }
            });
            Unload(resource);
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
                try
                {
                    RunIn(work.Resource, () => work.Callback(work.State));
                }
                catch (Exception error)
                {
                    Log.Write(LogLevel.Error, work.Resource.Name, $"Continuation failed: {Unwrap(error)}");
                }
            }

            foreach (ResourceContext resource in s_resources.Values.ToArray())
            {
                if (resource.Stopped)
                {
                    continue;
                }
                RunIn(resource, () =>
                {
                    foreach (ServerScript script in resource.Scripts)
                    {
                        try
                        {
                            script.OnTick();
                        }
                        catch (Exception error)
                        {
                            Log.Write(LogLevel.Error, resource.Name, $"OnTick failed: {Unwrap(error)}");
                        }
                    }
                });
            }
        }

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

        public static bool HasExport(string resourceName, string exportName) => s_resources.TryGetValue(resourceName, out ResourceContext? resource) && resource.Exports.ContainsKey(exportName);

        public static object? CallExport(string resourceName, string exportName, object?[] args)
        {
            if (!s_resources.TryGetValue(resourceName, out ResourceContext? resource) || !resource.Exports.TryGetValue(exportName, out Delegate? export))
            {
                throw new InvalidOperationException($"export '{exportName}' not found in resource '{resourceName}'");
            }
            string where = $"Export '{resourceName}.{exportName}'";
            object? result = null;
            RunIn(resource, () => result = ValueConverter.Settled(Invoke(export, args, where), where));
            return result;
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

        private static void Unload(ResourceContext resource)
        {
            resource.Stopped = true;
            resource.GlobalHandlers.Clear();
            resource.ClientHandlers.Clear();
            resource.Exports.Clear();
            resource.Scripts.Clear();
            s_resources.Remove(resource.Name);
            resource.LoadContext.Unload();
        }
    }
}
