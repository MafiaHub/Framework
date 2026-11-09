/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System.Threading.Tasks;
using MafiaHub.Framework.Scripting.Interop;
using MafiaHub.Framework.Scripting.Runtime;

namespace MafiaHub.Framework.Scripting
{
    /// <summary>
    /// A JavaScript function another resource handed over, as an event argument, an export's result or an export's
    /// argument. A parameter typed as a delegate (<c>Func&lt;double, double&gt;</c>, <c>Action&lt;string&gt;</c>) gets the
    /// same function converted.
    /// </summary>
    /// <remarks>
    /// Calls run on the scripting thread. The function stays callable while this object is reachable and its resource
    /// runs; after that, calling it throws.
    /// </remarks>
    public sealed class ScriptFunction
    {
        private readonly ulong _generation;

        internal ScriptFunction(ulong handle)
        {
            Handle = handle;
            _generation = HostEntry.Generation;
        }

        ~ScriptFunction()
        {
            ReleasedFunctions.Queue.Enqueue((_generation, Handle));
        }

        internal ulong Handle { get; }

        internal bool IsCurrent => _generation == HostEntry.Generation;

        /// <summary>Calls the function and returns what it returned. Throws when it returns a promise that has not settled; use <see cref="InvokeAsync"/> for those.</summary>
        public object? Invoke(params object?[] args) => HostEntry.CallFunction(this, args);

        /// <summary>Calls the function and converts what it returned.</summary>
        public T? Invoke<T>(params object?[] args) => (T?)ValueConverter.To(Invoke(args), typeof(T), "Function result");

        /// <summary>Calls the function; the task completes when a promise it returns settles.</summary>
        public Task<object?> InvokeAsync(params object?[] args) => HostEntry.CallFunctionAsync(this, args);

        /// <summary>Calls the function and converts what it settles with.</summary>
        public async Task<T?> InvokeAsync<T>(params object?[] args) => (T?)ValueConverter.To(await InvokeAsync(args), typeof(T), "Function result");
    }
}
