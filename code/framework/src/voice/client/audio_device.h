/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "spsc_ring.h"
#include "voice/voice_config.h"

#include <cstddef>
#include <cstdint>

// miniaudio.h expands to roughly four megabytes, so it stays out of this header.
struct ma_device;

namespace Framework::Voice {
    // ~340ms of mono capture.
    constexpr size_t kCaptureRingSamples = 16384;

    using CaptureRing = SpscRing<int16_t, kCaptureRingSamples>;

    // Microphone input. The device thread pushes s16 mono at kSampleRate into a ring; the
    // main thread drains whole frames. Nothing allocates or locks once Start() returns.
    class CaptureDevice final {
      public:
        CaptureDevice() = default;
        ~CaptureDevice();

        // The ring's address is handed to the device thread.
        CaptureDevice(const CaptureDevice &)            = delete;
        CaptureDevice &operator=(const CaptureDevice &) = delete;

        // False when there is no usable microphone; callers treat that as listen-only.
        bool Start();
        // Idempotent, and safe when Start() failed.
        void Stop();

        bool IsRunning() const {
            return _device != nullptr;
        }

        // Main thread. Pops exactly kFrameSamples, or writes nothing and returns false.
        bool ReadFrame(int16_t *out);

      private:
        static void OnCapture(ma_device *device, void *output, const void *input, uint32_t frameCount);

        ma_device *_device = nullptr;
        CaptureRing _ring;
    };

    // Speaker output. Pulls interleaved stereo float from a render function that runs on the
    // device thread, so that function must not allocate, lock, or touch networking.
    class PlaybackDevice final {
      public:
        // `stereoOut` holds frameCount * 2 floats and arrives zeroed. frameCount is whatever
        // the backend asks for, which is usually but not always kFrameSamples.
        using RenderFn = void (*)(float *stereoOut, uint32_t frameCount, void *user);

        PlaybackDevice() = default;
        ~PlaybackDevice();

        PlaybackDevice(const PlaybackDevice &)            = delete;
        PlaybackDevice &operator=(const PlaybackDevice &) = delete;

        // `render` and `user` must outlive the device.
        bool Start(RenderFn render, void *user);
        void Stop();

        bool IsRunning() const {
            return _device != nullptr;
        }

      private:
        static void OnPlayback(ma_device *device, void *output, const void *input, uint32_t frameCount);

        ma_device *_device = nullptr;
        RenderFn _render   = nullptr;
        void *_user        = nullptr;
    };
} // namespace Framework::Voice
