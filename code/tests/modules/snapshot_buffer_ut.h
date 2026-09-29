/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/snapshot_buffer.h"

MODULE(snapshot_buffer, {
    using namespace Framework::Utils;

    IT("returns false when empty", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot out;
        bool held = true;
        EQUALS(buffer.Empty(), true);
        EQUALS(buffer.Sample(1000, out, &held), false);
        EQUALS(held, false);
    });

    IT("latches a single sample regardless of render time", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap;
        snap.position = glm::vec3(5.0f, 0.0f, 0.0f);
        buffer.Push(snap, 1000);
        TransformSnapshot out;
        bool held = false;
        EQUALS(buffer.Sample(500, out, &held), true);
        EQUALS(held, true);
        EQUALS(out.position, snap.position);
        EQUALS(buffer.Sample(2000, out, &held), true);
        EQUALS(held, true);
        EQUALS(out.position, snap.position);
    });

    IT("interpolates position between bracketing samples", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.position = glm::vec3(0.0f);
        a.velocity = glm::vec3(2.0f, 0.0f, 0.0f); // already moving, so the 10u step is not a teleport
        b.position = glm::vec3(10.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        bool held = true;
        EQUALS(buffer.Sample(1050, out, &held), true);
        EQUALS(held, false);
        EQUALS(out.position, glm::vec3(5.0f, 0.0f, 0.0f));
    });

    IT("derives velocity from bracket displacement", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.position = glm::vec3(0.0f);
        a.velocity = glm::vec3(2.0f, 0.0f, 0.0f); // already moving, so the 10u step is not a teleport
        b.position = glm::vec3(10.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        EQUALS(buffer.Sample(1050, out), true);
        EQUALS(out.velocity, glm::vec3(100.0f, 0.0f, 0.0f));
    });

    IT("holds the oldest sample behind the buffer", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.position = glm::vec3(1.0f, 0.0f, 0.0f);
        b.position = glm::vec3(2.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        bool held = false;
        EQUALS(buffer.Sample(500, out, &held), true);
        EQUALS(held, true);
        EQUALS(out.position, a.position);
    });

    IT("extrapolates along velocity on underrun", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.position = glm::vec3(0.0f);
        b.position = glm::vec3(1.0f, 0.0f, 0.0f);
        b.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        bool held = true;
        EQUALS(buffer.Sample(1200, out, &held), true); // 100ms past newest at 10 u/s -> +1.0
        EQUALS(held, false);
        EQUALS(out.position, glm::vec3(2.0f, 0.0f, 0.0f));
    });

    IT("caps extrapolation at maxExtrapolationMs", {
        SnapshotBufferConfig config;
        config.maxExtrapolationMs = 100.0f;
        TransformSnapshotBuffer buffer(&config);
        TransformSnapshot a, b;
        b.position = glm::vec3(1.0f, 0.0f, 0.0f);
        b.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        bool held = true;
        EQUALS(buffer.Sample(1199, out, &held), true);
        EQUALS(held, false);
        EQUALS(buffer.Sample(1200, out, &held), true);
        EQUALS(held, true);
        EQUALS(buffer.Sample(5000, out, &held), true); // way past newest, clamped to 100ms -> +1.0
        EQUALS(held, true);
        EQUALS(out.position, glm::vec3(2.0f, 0.0f, 0.0f));

        b.position = glm::vec3(2.0f, 0.0f, 0.0f);
        buffer.Push(b, 1200);
        EQUALS(buffer.Sample(1250, out, &held), true);
        EQUALS(held, false);
    });

    IT("treats a large jump as teleport and clears history", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.position = glm::vec3(0.0f);
        b.position = glm::vec3(1000.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        EQUALS(buffer.Sample(1050, out), true); // single-sample latch, no sweep across the map
        EQUALS(out.position, b.position);
    });

    IT("ignores out-of-order and duplicate timestamps", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.position = glm::vec3(1.0f, 0.0f, 0.0f);
        b.position = glm::vec3(2.0f, 0.0f, 0.0f);
        buffer.Push(a, 1000);
        buffer.Push(b, 1000);
        buffer.Push(b, 900);
        TransformSnapshot out;
        EQUALS(buffer.Sample(2000, out), true);
        EQUALS(out.position, a.position);
    });

    IT("slerps rotation between samples", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b;
        a.rotation = glm::angleAxis(0.0f, glm::vec3(0.0f, 0.0f, 1.0f));
        b.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        TransformSnapshot out;
        EQUALS(buffer.Sample(1050, out), true);
        const float angle = glm::angle(out.rotation);
        LESSER(std::fabs(angle - glm::quarter_pi<float>()), 1e-4f);
    });

    IT("adapts the effective delay to interval and jitter within bounds", {
        SnapshotBufferConfig config;
        config.adaptiveDelay = true;
        config.minDelayMs    = 50.0f;
        config.maxDelayMs    = 200.0f;
        TransformSnapshotBuffer buffer(&config);
        TransformSnapshot snap;
        for (int i = 0; i < 10; ++i) {
            buffer.Push(snap, 1000 + i * 100); // steady 100ms interval, no jitter
        }
        GREATEREQ(buffer.EffectiveDelayMs(), config.minDelayMs);
        LESSEREQ(buffer.EffectiveDelayMs(), config.maxDelayMs);

        config.adaptiveDelay = false;
        EQUALS(buffer.EffectiveDelayMs(), config.interpDelayMs);
    });

    IT("keeps a moving body advancing when a late snapshot increases delay", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap, before, after;
        snap.velocity = glm::vec3(1.0f, 0.0f, 0.0f);
        for (MafiaNet::Time time = 1000; time <= 1100; time += 50) {
            snap.position.x = static_cast<float>(time - 1000) / 1000.0f;
            buffer.Push(snap, time);
        }
        const auto previous = buffer.RenderTime(1199);
        buffer.Sample(previous, before);
        snap.position.x = 0.2f;
        buffer.Push(snap, 1200); // One missing update increases the adaptive delay.
        const auto current = buffer.RenderTime(1215);
        // The former now-delay clock goes backwards from 1149 to 1145.
        LESSER(1215.0f - buffer.EffectiveDelayMs(), static_cast<float>(previous));
        GREATEREQ(current - previous, 14U);
        LESSEREQ(current - previous, 18U);
        buffer.Sample(current, after);
        GREATER(after.position.x, before.position.x);
        LESSER(after.position.x - before.position.x, 0.018f);
    });

    IT("bounds catch-up speed and converges without losing fractional milliseconds", {
        SnapshotBufferConfig config;
        config.adaptiveDelay = false;
        config.interpDelayMs = 100.0f;
        TransformSnapshotBuffer buffer(&config);
        auto previous        = buffer.RenderTime(1000);
        config.interpDelayMs = 50.0f;
        for (MafiaNet::Time now = 1001; now <= 2000; ++now) {
            const auto current = buffer.RenderTime(now);
            GREATEREQ(current, previous);
            LESSEREQ(current - previous, 2U);
            previous = current;
        }
        EQUALS(previous, 1950U);
        EQUALS(buffer.RenderTime(2000), previous); // Sampling twice in a frame cannot advance time.
        EQUALS(buffer.RenderTime(1999), previous);
    });

    IT("resets adaptive timing and the presentation clock when history is cleared", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap;
        buffer.Push(snap, 1000);
        buffer.Push(snap, 1200);
        EQUALS(buffer.EffectiveDelayMs(), 200.0f);
        EQUALS(buffer.RenderTime(1500), 1300U);
        buffer.Clear();
        EQUALS(buffer.EffectiveDelayMs(), 100.0f);
        EQUALS(buffer.RenderTime(1500), 1400U);
        buffer.Clear();
        EQUALS(buffer.RenderTime(10), 0U); // No unsigned clock underflow at startup.
        // Held at zero until the delay has elapsed, then starting on the target rather than ahead of it.
        EQUALS(buffer.RenderTime(100), 0U);
        EQUALS(buffer.RenderTime(110), 10U);
        EQUALS(buffer.RenderTime(120), 20U);
    });

    IT("starts fresh timing history after a teleport", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap, out;
        buffer.Push(snap, 1000);
        buffer.Push(snap, 1200);
        buffer.RenderTime(1300);
        snap.position.x = 1000.0f;
        buffer.Push(snap, 1400);
        EQUALS(buffer.EffectiveDelayMs(), 100.0f);
        EQUALS(buffer.RenderTime(1400), 1300U);
        buffer.Sample(1300, out);
        EQUALS(out.position.x, 1000.0f);
    });
});
