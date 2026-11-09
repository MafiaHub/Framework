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
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using System.Threading;
using MafiaHub.Framework.Scripting.Interop;

namespace MafiaHub.Framework.Scripting.Runtime
{
    /// <summary>
    /// A resource's assemblies, unloaded when it stops. Assemblies are read from memory, so the files can be replaced
    /// while the server runs; this API resolves to the server's own copy so scripts and the host share its types.
    /// </summary>
    internal sealed class ResourceLoadContext : AssemblyLoadContext
    {
        private readonly string _directory;

        public ResourceLoadContext(string name, string directory) : base($"resource:{name}", isCollectible: true)
        {
            _directory = directory;
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name == typeof(ResourceLoadContext).Assembly.GetName().Name)
            {
                return typeof(ResourceLoadContext).Assembly;
            }
            string candidate = Path.Combine(_directory, assemblyName.Name + ".dll");
            return File.Exists(candidate) ? LoadFromFile(candidate) : null;
        }

        public Assembly LoadFromFile(string path)
        {
            using var assembly = new MemoryStream(File.ReadAllBytes(path));
            string symbols = Path.ChangeExtension(path, ".pdb");
            if (File.Exists(symbols))
            {
                using var pdb = new MemoryStream(File.ReadAllBytes(symbols));
                return LoadFromStream(assembly, pdb);
            }
            return LoadFromStream(assembly);
        }
    }

    /// <summary>
    /// Runs a resource's continuations on the scripting thread, at the next tick. Being current also names the
    /// resource that registered an event handler or an export.
    /// </summary>
    internal sealed class ResourceSynchronizationContext : SynchronizationContext
    {
        public ResourceSynchronizationContext(ResourceContext resource)
        {
            Resource = resource;
        }

        public ResourceContext Resource { get; }

        public override void Post(SendOrPostCallback callback, object? state) => ScriptHost.Post(Resource, callback, state);

        public override void Send(SendOrPostCallback callback, object? state)
        {
            if (HostEntry.IsScriptingThread)
            {
                callback(state);
                return;
            }
            throw new InvalidOperationException("Synchronous calls into a script from another thread are not supported; await instead.");
        }

        public override SynchronizationContext CreateCopy() => this;
    }

    internal sealed class EventHandlerEntry
    {
        public EventHandlerEntry(string name, Delegate handler, bool once)
        {
            Name = name;
            Handler = handler;
            Once = once;
        }

        public string Name { get; }
        public Delegate Handler { get; }
        public bool Once { get; }
    }

    internal sealed class ResourceContext
    {
        public ResourceContext(string name, string directory)
        {
            Name = name;
            LoadContext = new ResourceLoadContext(name, directory);
            Sync = new ResourceSynchronizationContext(this);
        }

        public string Name { get; }
        public ResourceLoadContext LoadContext { get; }
        public ResourceSynchronizationContext Sync { get; }
        public List<ServerScript> Scripts { get; } = new List<ServerScript>();
        public Dictionary<string, List<EventHandlerEntry>> GlobalHandlers { get; } = new Dictionary<string, List<EventHandlerEntry>>();
        public Dictionary<string, List<EventHandlerEntry>> ClientHandlers { get; } = new Dictionary<string, List<EventHandlerEntry>>();
        public Dictionary<string, Delegate> Exports { get; } = new Dictionary<string, Delegate>();
        public bool Stopped { get; set; }

        public Dictionary<string, List<EventHandlerEntry>> Handlers(EventScope scope) => scope == EventScope.Client ? ClientHandlers : GlobalHandlers;
    }

    internal readonly record struct PostedWork(ResourceContext Resource, SendOrPostCallback Callback, object? State);

    internal static class PostQueue
    {
        public static readonly ConcurrentQueue<PostedWork> Items = new ConcurrentQueue<PostedWork>();
    }
}
