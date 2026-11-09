/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

using System.Threading;
using System.Threading.Tasks;
using MafiaHub.Framework.Scripting;

namespace LeakyResource
{
    // Ignores its stop: a raw thread and background work that outlive it, keeping its code loaded for a while.
    public sealed class Leaky : ServerScript
    {
        protected override void OnStart()
        {
            new Thread(Linger) { IsBackground = true, Name = "leaky" }.Start();
            Background("ignores-stop", _ => Task.Delay(1500));
        }

        private static void Linger() => Thread.Sleep(1500);
    }
}
