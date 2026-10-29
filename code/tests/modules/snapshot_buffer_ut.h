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

    IT("keeps estimating through an idle pause and rests until the sender resumes", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap, out;
        for (MafiaNet::Time time = 1000; time <= 1500; time += 50) {
            buffer.Push(snap, time);
        }
        const float delay = buffer.EffectiveDelayMs();
        snap.position.x   = 0.1f;
        buffer.Push(snap, 4500); // Three seconds idle, then moving again.
        EQUALS(buffer.EffectiveDelayMs(), delay);
        EQUALS(buffer.IntervalMs(), 50.0f);
        buffer.Sample(4400.0, out); // Still resting where it stopped, not part way across the pause.
        EQUALS(out.position.x, 0.0f);
        buffer.Sample(4475.0, out);
        GREATER(out.position.x, 0.0f);
    });

    IT("moves the presentation clock without stepping when the sender changes rate band", {
        SnapshotBufferConfig config;
        config.minDelayMs     = 0.0f;
        config.maxDelayMs     = 600.0f;
        config.delayIntervals = 2.0f;
        TransformSnapshotBuffer buffer(&config);
        TransformSnapshot snap;
        snap.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
        // 10 m/s, sampled every 33 ms, then every 133 ms from 3 s on (a farther band), arriving 40 ms late.
        MafiaNet::Time sent = 1000;
        double previous     = -1.0;
        float lastX         = -1.0f;
        float maxStep       = 0.0f;
        int heldFrames      = 0;
        for (double now = 1000.0; now < 9000.0; now += 16.0) {
            while (static_cast<double>(sent) + 40.0 <= now) {
                snap.position.x = static_cast<float>(sent - 1000) * 0.01f;
                buffer.Push(snap, sent, static_cast<double>(sent) + 40.0);
                sent += sent < 4000 ? 33 : 133;
            }
            const double renderTime = buffer.RenderTimeMs(now);
            if (renderTime <= 0.0) {
                continue;
            }
            if (previous >= 0.0) {
                // Never backwards, and never faster than wall time plus twice the slew.
                GREATEREQ(renderTime, previous);
                LESSEREQ(renderTime - previous, 16.0 * 1.21);
            }
            previous = renderTime;
            TransformSnapshot pose;
            bool held = false;
            buffer.Sample(renderTime, pose, &held);
            // Past the startup latch on the first sample.
            if (now < 1500.0) {
                lastX = pose.position.x;
                continue;
            }
            heldFrames += held ? 1 : 0;
            if (lastX >= 0.0f) {
                maxStep = std::max(maxStep, std::fabs(pose.position.x - lastX));
            }
            lastX = pose.position.x;
        }
        // 16 ms at 10 m/s is 0.16 m a frame; slewing may stretch that by at most a fifth.
        LESSEREQ(maxStep, 0.2f);
        EQUALS(heldFrames, 0);
        // Settled on the slow band: two 133 ms intervals plus the 40 ms lateness.
        GREATEREQ(buffer.EffectiveDelayMs(), 2.0f * 133.0f + 40.0f - 5.0f);
    });

    IT("glides through a stall of a moving sender without snapping back, and raises the delay", {
        SnapshotBufferConfig config;
        config.maxExtrapolationMs = 500.0f;
        TransformSnapshotBuffer buffer(&config);
        TransformSnapshot snap;
        snap.velocity       = glm::vec3(10.0f, 0.0f, 0.0f);
        MafiaNet::Time sent = 1000;
        float lastX         = -1.0f;
        float maxStep       = 0.0f;
        float delayBefore   = 0.0f;
        float delayAfter    = 0.0f;
        for (double now = 1000.0; now < 5000.0; now += 16.0) {
            while (static_cast<double>(sent) <= now) {
                snap.position.x = static_cast<float>(sent - 1000) * 0.01f;
                buffer.Push(snap, sent, static_cast<double>(sent));
                // One 400 ms stall at 3 s: the samples in it never come.
                sent += sent == 3000 ? 400 : 50;
            }
            if (now < 3000.0) {
                delayBefore = buffer.EffectiveDelayMs();
            }
            else if (now < 3600.0) {
                delayAfter = std::max(delayAfter, buffer.EffectiveDelayMs());
            }
            const double renderTime = buffer.RenderTimeMs(now);
            if (renderTime <= 0.0) {
                continue;
            }
            TransformSnapshot pose;
            buffer.Sample(renderTime, pose);
            if (now >= 1500.0 && lastX >= 0.0f) {
                GREATEREQ(pose.position.x, lastX);
                maxStep = std::max(maxStep, pose.position.x - lastX);
            }
            lastX = pose.position.x;
        }
        // 0.16 m a frame at 10 m/s; the render clock may run up to a fifth fast while it slews.
        LESSEREQ(maxStep, 0.2f);
        // The stall counts as jitter: three times a tenth of its 350 ms excess.
        GREATER(delayAfter, delayBefore + 90.0f);
    });

    IT("rests through a long gap when the pose stood still despite its velocity", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap, out;
        snap.velocity = glm::vec3(2.0f, 0.0f, 0.0f); // Walking into a wall: velocity, no motion.
        for (MafiaNet::Time time = 1000; time <= 1500; time += 50) {
            buffer.Push(snap, time);
        }
        snap.position.x = 0.1f;
        buffer.Push(snap, 3500);
        EQUALS(buffer.IntervalMs(), 50.0f);
        buffer.Sample(3400.0, out);
        EQUALS(out.position.x, 0.0f);
    });

    IT("learns a rate band slower than the restart gap without stopping a moving body", {
        SnapshotBufferConfig config;
        config.maxDelayMs         = 800.0f;
        config.maxExtrapolationMs = 400.0f;
        TransformSnapshotBuffer buffer(&config);
        TransformSnapshot snap;
        snap.velocity = glm::vec3(10.0f, 0.0f, 0.0f);
        // 100 ms (the near band), then 317 ms from 3 s on: a 200 ms band behind a ~117 ms host cadence.
        MafiaNet::Time sent = 1000;
        float lastX         = -1.0f;
        float maxStep       = 0.0f;
        int heldFrames      = 0;
        for (double now = 1000.0; now < 12000.0; now += 16.0) {
            while (static_cast<double>(sent) <= now) {
                snap.position.x = static_cast<float>(sent - 1000) * 0.01f;
                buffer.Push(snap, sent, static_cast<double>(sent));
                sent += sent < 3000 ? 100 : 317;
            }
            const double renderTime = buffer.RenderTimeMs(now);
            if (renderTime <= 0.0) {
                continue;
            }
            TransformSnapshot pose;
            bool held = false;
            buffer.Sample(renderTime, pose, &held);
            if (now < 1500.0) {
                lastX = pose.position.x;
                continue;
            }
            heldFrames += held ? 1 : 0;
            GREATEREQ(pose.position.x, lastX);
            maxStep = std::max(maxStep, pose.position.x - lastX);
            lastX   = pose.position.x;
        }
        LESSEREQ(maxStep, 0.2f);
        EQUALS(heldFrames, 0);
        GREATER(buffer.IntervalMs(), 250.0f);
    });

    IT("samples fractional render times between whole milliseconds", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot a, b, out;
        a.velocity   = glm::vec3(1.0f, 0.0f, 0.0f);
        b.position.x = 1.0f;
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        buffer.Sample(1050.5, out);
        LESSER(std::fabs(out.position.x - 0.505f), 0.0001f);
    });
});
