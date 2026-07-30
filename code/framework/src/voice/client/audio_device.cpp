/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "audio_device.h"

#include <logging/logger.h>
#include <miniaudio.h>

#include <cstring>
#include <new>

namespace Framework::Voice {
    namespace {
        // The period is a latency hint, not a contract: a backend may ignore it, and the
        // rings decouple both directions anyway.
        void ApplyCommonConfig(ma_device_config &config) {
            config.sampleRate                = kSampleRate;
            config.periodSizeInFrames        = kFrameSamples;
            config.performanceProfile        = ma_performance_profile_low_latency;
            config.noPreSilencedOutputBuffer = MA_TRUE;
        }
    } // namespace

    void CaptureDevice::OnCapture(ma_device *device, void *output, const void *input, uint32_t frameCount) {
        (void)output;

        auto *self = static_cast<CaptureDevice *>(device->pUserData);
        if (self == nullptr || input == nullptr) {
            return;
        }

        // A failed Push means the main thread has not drained for a third of a second.
        // Dropping beats stalling the device thread.
        self->_ring.Push(static_cast<const int16_t *>(input), frameCount);
    }

    bool CaptureDevice::Start() {
        if (_device != nullptr) {
            return true;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format   = ma_format_s16;
        config.capture.channels = kChannels;
        config.dataCallback     = &CaptureDevice::OnCapture;
        config.pUserData        = this;
        ApplyCommonConfig(config);

        auto *device = new (std::nothrow) ma_device {};
        if (device == nullptr) {
            return false;
        }

        if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
            delete device;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: no usable capture device; continuing listen-only");
            return false;
        }

        if (ma_device_start(device) != MA_SUCCESS) {
            ma_device_uninit(device);
            delete device;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: capture device failed to start; continuing listen-only");
            return false;
        }

        _device = device;
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice: capture device '{}' running at {}Hz", _device->capture.name, _device->sampleRate);
        return true;
    }

    void CaptureDevice::Stop() {
        if (_device == nullptr) {
            return;
        }

        // uninit joins the device thread, so no callback can be in flight afterwards.
        ma_device_uninit(_device);
        delete _device;
        _device = nullptr;
        _ring.Clear();
    }

    CaptureDevice::~CaptureDevice() {
        Stop();
    }

    bool CaptureDevice::ReadFrame(int16_t *out) {
        return _ring.Pop(out, kFrameSamples);
    }

    void PlaybackDevice::OnPlayback(ma_device *device, void *output, const void *input, uint32_t frameCount) {
        (void)input;

        auto *self = static_cast<PlaybackDevice *>(device->pUserData);
        if (self == nullptr || output == nullptr) {
            return;
        }

        auto *stereoOut = static_cast<float *>(output);

        // noPreSilencedOutputBuffer is set, and the mixer accumulates.
        std::memset(stereoOut, 0, static_cast<size_t>(frameCount) * 2 * sizeof(float));

        if (self->_render != nullptr) {
            self->_render(stereoOut, frameCount, self->_user);
        }
    }

    bool PlaybackDevice::Start(RenderFn render, void *user) {
        if (_device != nullptr) {
            return true;
        }

        if (render == nullptr) {
            return false;
        }

        _render = render;
        _user   = user;

        ma_device_config config  = ma_device_config_init(ma_device_type_playback);
        config.playback.format   = ma_format_f32;
        config.playback.channels = 2;
        config.dataCallback      = &PlaybackDevice::OnPlayback;
        config.pUserData         = this;
        ApplyCommonConfig(config);

        auto *device = new (std::nothrow) ma_device {};
        if (device == nullptr) {
            return false;
        }

        if (ma_device_init(nullptr, &config, device) != MA_SUCCESS) {
            delete device;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: no usable playback device; remote speakers will be inaudible");
            return false;
        }

        if (ma_device_start(device) != MA_SUCCESS) {
            ma_device_uninit(device);
            delete device;
            Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: playback device failed to start; remote speakers will be inaudible");
            return false;
        }

        _device = device;
        Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice: playback device '{}' running at {}Hz", _device->playback.name, _device->sampleRate);
        return true;
    }

    void PlaybackDevice::Stop() {
        if (_device == nullptr) {
            return;
        }

        // uninit joins the device thread, so the render function cannot be running once this
        // returns -- which is what lets the owner tear down the state it reads.
        ma_device_uninit(_device);
        delete _device;
        _device = nullptr;
    }

    PlaybackDevice::~PlaybackDevice() {
        Stop();
    }
} // namespace Framework::Voice
