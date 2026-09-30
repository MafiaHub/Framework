/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/button_state.h>
#include <input/resource_holds.h>

MODULE(input_state, {
    IT("held keys repeat without creating new press edges", {
        Framework::Input::ButtonState<256> keys;
        keys.Set('W', true);
        EQUALS(keys.IsPressed('W'), true);
        keys.ClearEdges();
        keys.Set('W', true);
        EQUALS(keys.IsDown('W'), true);
        EQUALS(keys.IsPressed('W'), false);
        keys.Set('W', false);
        EQUALS(keys.IsReleased('W'), true);
        keys.ClearEdges();
        keys.Set('W', false);
        EQUALS(keys.IsReleased('W'), false);
    });
    IT("retains a quick tap's down and up edges for all frame consumers", {
        Framework::Input::ButtonState<256> keys;
        keys.Set('T', true);
        keys.Set('T', false);
        EQUALS(keys.IsPressed('T'), true);
        EQUALS(keys.IsReleased('T'), true);
        EQUALS(keys.IsDown('T'), false);
        keys.ClearEdges();
        EQUALS(keys.IsPressed('T'), false);
        EQUALS(keys.IsReleased('T'), false);
    });
    IT("rejects invalid caller supplied indices", {
        Framework::Input::ButtonState<5> buttons;
        for (int index : {-1, 5, 256}) {
            buttons.Set(index, true);
            EQUALS(buttons.IsDown(index), false);
            EQUALS(buttons.IsUp(index), false);
            EQUALS(buttons.IsPressed(index), false);
            EQUALS(buttons.IsReleased(index), false);
        }
    });
    IT("releases held state while leaving independent keys alone", {
        Framework::Input::ButtonState<256> keys;
        keys.Set('W', true);
        keys.Set('A', true);
        keys.ClearEdges();
        keys.ReleaseAll();
        EQUALS(keys.IsDown('W'), false);
        EQUALS(keys.IsReleased('W'), true);
        EQUALS(keys.IsReleased('A'), true);
        EQUALS(keys.IsReleased('S'), false);
    });
    IT("can seed a repeated key after focus acquisition without a toggle edge", {
        Framework::Input::ButtonState<256> keys;
        keys.Set('T', true, false);
        EQUALS(keys.IsDown('T'), true);
        EQUALS(keys.IsPressed('T'), false);
    });
    IT("snapshots track physical transitions independently of consumers", {
        Framework::Input::KeySnapshot snapshot;
        bool held       = false;
        const auto read = [&held](int key) {
            return key == 'T' && held;
        };
        snapshot.Update(read, true);
        held = true;
        snapshot.Update(read, true);
        EQUALS(snapshot.IsPressed('T'), true);
        EQUALS(snapshot.IsPressed('T'), true);
        snapshot.Update(read, true);
        EQUALS(snapshot.IsPressed('T'), false);
        held = false;
        snapshot.Update(read, true);
        EQUALS(snapshot.IsReleased('T'), true);
    });
    IT("snapshot acquisition and foreground regain swallow already held keys", {
        Framework::Input::KeySnapshot snapshot;
        const auto held = [](int key) {
            return key == 'W';
        };
        snapshot.Update(held, true);
        EQUALS(snapshot.IsDown('W'), true);
        EQUALS(snapshot.IsPressed('W'), false);
        snapshot.Update(held, false);
        EQUALS(snapshot.IsDown('W'), false);
        EQUALS(snapshot.IsReleased('W'), false);
        snapshot.Update(held, true);
        EQUALS(snapshot.IsDown('W'), true);
        EQUALS(snapshot.IsPressed('W'), false);
    });
    IT("unavailable device state discards edges and reseeds on resume", {
        Framework::Input::KeySnapshot snapshot;
        bool held       = false;
        const auto read = [&held](int key) {
            return key == 'T' && held;
        };
        snapshot.Update(read, true);
        held = true;
        snapshot.Update(read, false);
        EQUALS(snapshot.IsPressed('T'), false);
        snapshot.Update(read, true);
        EQUALS(snapshot.IsDown('T'), true);
        EQUALS(snapshot.IsPressed('T'), false);
        snapshot.Update(read, false);
        EQUALS(snapshot.IsDown('T'), false);
        held = false;
        snapshot.Update(read, false);
        EQUALS(snapshot.IsReleased('T'), false);
        snapshot.Update(read, true);
        EQUALS(snapshot.IsDown('T'), false);
        EQUALS(snapshot.IsReleased('T'), false);
        held = true;
        snapshot.Update(read, true);
        EQUALS(snapshot.IsPressed('T'), true);
    });
    IT("one resource cannot release another resource's hold", {
        Framework::Input::ResourceHolds holds;
        holds.Acquire("panel-a");
        holds.Acquire("panel-b");
        EQUALS(holds.Release("missing"), false);
        EQUALS(holds.Release("panel-a"), true);
        EQUALS(holds.Release("panel-a"), false);
        EQUALS(holds.IsHeld("panel-b"), true);
        EQUALS(holds.IsHeld(), true);
    });
    IT("resource stop drains all of its counted holds exactly once", {
        Framework::Input::ResourceHolds holds;
        holds.Acquire("panel-a");
        holds.Acquire("panel-a");
        holds.Acquire("panel-b");
        EQUALS(holds.TakeAll("panel-a"), 2);
        EQUALS(holds.TakeAll("panel-a"), 0);
        EQUALS(holds.TakeAll("panel-b"), 1);
        EQUALS(holds.IsHeld(), false);
    });
    IT("forced cleanup drops ownership so a later stop cannot unlock a new owner", {
        Framework::Input::ResourceHolds holds;
        holds.Acquire("old");
        holds.Clear();
        holds.Acquire("new");
        EQUALS(holds.TakeAll("old"), 0);
        EQUALS(holds.IsHeld("new"), true);
    });
    IT("setEnabled-style ownership is idempotent and independent per resource", {
        Framework::Input::ResourceHolds holds;
        holds.Set("panel-a", true);
        holds.Set("panel-a", true);
        holds.Set("panel-b", true);
        EQUALS(holds.TakeAll("panel-a"), 1);
        holds.Set("panel-a", false);
        EQUALS(holds.IsHeld("panel-b"), true);
        holds.Set("panel-b", false);
        EQUALS(holds.IsHeld(), false);
    });
});
