/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/push_to_talk_gate.h"

MODULE(voice_ptt_gate, {
    using namespace Framework::Voice;

    IT("keeps the gate open while the key is held", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(100);
        gate.SetHeld(true, 1000);
        EQUALS(gate.IsOpen(1000), true);
        EQUALS(gate.IsOpen(9000), true);
        EQUALS(gate.IsHeld(), true);
    });

    IT("holds the gate open for the release delay, then closes", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(100);
        gate.SetHeld(true, 1000);
        gate.SetHeld(false, 1000);

        EQUALS(gate.IsHeld(), false);
        EQUALS(gate.IsOpen(1050), true);
        EQUALS(gate.IsOpen(1099), true);
        EQUALS(gate.IsOpen(1100), false);
    });

    IT("cancels a pending release when the key is pressed again", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(100);
        gate.SetHeld(true, 1000);
        gate.SetHeld(false, 1000);
        gate.SetHeld(true, 1050);

        EQUALS(gate.IsOpen(1100), true);
        gate.SetHeld(false, 1200);
        EQUALS(gate.IsOpen(1299), true);
        EQUALS(gate.IsOpen(1300), false);
    });

    IT("arms the tail on the release edge only", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(100);
        gate.SetHeld(true, 1000);
        gate.SetHeld(false, 1000);

        // A repeat must not push the deadline forward, or the gate would never close.
        gate.SetHeld(false, 1050);
        EQUALS(gate.IsOpen(1100), false);
    });

    IT("closes immediately on a cut, tail or not", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(100);
        gate.SetHeld(true, 1000);
        gate.SetHeld(false, 1000);
        gate.Cut();
        EQUALS(gate.IsOpen(1050), false);

        gate.SetHeld(true, 2000);
        gate.Cut();
        EQUALS(gate.IsHeld(), false);
        EQUALS(gate.IsOpen(2000), false);
    });

    IT("does not re-arm a tail from a cut key state", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(100);
        gate.SetHeld(true, 1000);
        gate.Cut();

        // The key goes up a tick later, but the cut already consumed the edge.
        gate.SetHeld(false, 1010);
        EQUALS(gate.IsOpen(1010), false);
    });

    IT("stops on release when the delay is zero", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(0);
        gate.SetHeld(true, 1000);
        gate.SetHeld(false, 1000);
        EQUALS(gate.IsOpen(1000), false);
    });

    IT("clamps the delay to the configured ceiling", {
        PushToTalkGate gate;
        gate.SetReleaseDelay(kMaxPushToTalkReleaseMs + 5000);
        EQUALS(gate.GetReleaseDelay(), kMaxPushToTalkReleaseMs);

        gate.SetHeld(true, 0);
        gate.SetHeld(false, 0);
        EQUALS(gate.IsOpen(static_cast<int64_t>(kMaxPushToTalkReleaseMs) - 1), true);
        EQUALS(gate.IsOpen(static_cast<int64_t>(kMaxPushToTalkReleaseMs)), false);
    });

    IT("defaults to the framework's release delay", {
        PushToTalkGate gate;
        EQUALS(gate.GetReleaseDelay(), kDefaultPushToTalkReleaseMs);
        EQUALS(gate.IsHeld(), false);
        EQUALS(gate.IsOpen(0), false);
    });
});
