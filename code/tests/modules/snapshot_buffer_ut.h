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
        EQUALS(buffer.Empty(), true);
        EQUALS(buffer.Sample(1000, out), false);
    });

    IT("latches a single sample regardless of render time", {
        TransformSnapshotBuffer buffer;
        TransformSnapshot snap;
        snap.position = glm::vec3(5.0f, 0.0f, 0.0f);
        buffer.Push(snap, 1000);
        TransformSnapshot out;
        EQUALS(buffer.Sample(500, out), true);
        EQUALS(out.position, snap.position);
        EQUALS(buffer.Sample(2000, out), true);
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
        EQUALS(buffer.Sample(1050, out), true);
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
        EQUALS(buffer.Sample(500, out), true);
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
        EQUALS(buffer.Sample(1200, out), true); // 100ms past newest at 10 u/s -> +1.0
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
        EQUALS(buffer.Sample(5000, out), true); // way past newest, clamped to 100ms -> +1.0
        EQUALS(out.position, glm::vec3(2.0f, 0.0f, 0.0f));
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
});
