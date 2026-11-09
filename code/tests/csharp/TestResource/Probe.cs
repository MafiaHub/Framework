/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using MafiaHub.Framework.Scripting;

namespace TestResource
{
    // Reports what it sees as events a JavaScript resource records, and offers exports for it to call.
    public sealed class Probe : ServerScript
    {
        private int _ticks;
        private IDisposable? _counted;
        private int _count;

        protected override void OnStart()
        {
            Exports.Register("add", (double a, double b) => a + b);
            Exports.Register("echo", (object? value) => value);
            Exports.Register("describe", (Dictionary<string, object?> map, List<int> numbers) => $"{map.Count}:{numbers.Count}:{numbers[numbers.Count - 1]}");
            Exports.Register("vector", (Vector3 v) => new Vector3(v.X * 2, v.Y * 2, v.Z * 2));
            Exports.Register("fail", new Func<int>(() => throw new InvalidOperationException("refused on purpose")));
            Exports.Register("ready", async () =>
            {
                await Task.CompletedTask;
                return 42;
            });
            Exports.Register("pending", async () =>
            {
                await Task.Delay(1000);
                return 1;
            });

            Events.On("js:ping", (string text, int n) => Events.Emit("cs:pong", text + "!", n * 2));
            Events.On("js:veto", () => false);
            Events.On("js:callJs", () => Events.Emit("cs:called", Exports.Call<double>("jsres", "mul", 6, 7)));
            Events.On("js:callJsAsync", () => Events.Emit("cs:calledAsync", Exports.Call<int>("jsres", "settled")));
            Events.On("js:callJsPending", () =>
            {
                try
                {
                    Exports.Call("jsres", "unsettled");
                    Events.Emit("cs:pendingAnswered");
                }
                catch (InvalidOperationException error)
                {
                    Events.Emit("cs:pendingRefused", error.Message.Contains("has not settled") ? 1 : 0);
                }
            });
            Events.On("js:delay", async () =>
            {
                await Task.Delay(5);
                Events.Emit("cs:delayed", Environment.CurrentManagedThreadId == _threadId ? 1 : 0);
            });
            Events.On("js:throw", new Action(() => throw new InvalidOperationException("handler failed on purpose")));
            Events.On("js:entity", (Entity? entity) => Events.Emit("cs:entity", entity is null ? -1 : 0, entity is null ? 0 : entity.Position.X));
            Events.Once("js:once", () => Events.Emit("cs:once", ++_count));
            _counted = Events.On("js:counted", () => Events.Emit("cs:counted", ++_count));
            Events.On("js:unsubscribe", () => _counted?.Dispose());
            Events.OnClient("js:fromClient", (string text) => Events.Emit("cs:fromClient", text.Length));
            Events.On("js:emitFromWorker", async () =>
            {
                await Task.Run(() =>
                {
                    try
                    {
                        Events.Emit("cs:fromWorker");
                    }
                    catch (InvalidOperationException)
                    {
                        _workerRefused = true;
                    }
                });
                Events.Emit("cs:workerRefused", _workerRefused ? 1 : 0);
            });

            _threadId = Environment.CurrentManagedThreadId;
            Events.Emit("cs:started", ResourceName.Length);
        }

        private int _threadId;
        private bool _workerRefused;

        protected override void OnTick()
        {
            if (++_ticks == 3)
            {
                Events.Emit("cs:ticked", _ticks);
            }
        }

        protected override void OnStop()
        {
            Events.Emit("cs:stopped", ResourceName.Length);
        }
    }
}
