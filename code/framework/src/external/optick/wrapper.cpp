/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

namespace Framework::External::Optick {
    void Wrapper::StartCapture() {
        OPTICK_START_CAPTURE();
    }

    void Wrapper::StopCapture() {
        OPTICK_STOP_CAPTURE();
    }

    void Wrapper::Dump(const char *path) {
        OPTICK_SAVE_CAPTURE(path);
    }

    void Wrapper::Update() {
        OPTICK_UPDATE();
    }

    void Wrapper::Shutdown() {
        OPTICK_SHUTDOWN();
    }

    void Wrapper::SetAllocator(::Optick::AllocateFn allocateFn, ::Optick::DeallocateFn deallocateFn, ::Optick::InitThreadCb initThreadCb) {
        OPTICK_SET_MEMORY_ALLOCATOR(allocateFn, deallocateFn, initThreadCb);
    }

    void Wrapper::SetStateChangedCallback(::Optick::StateCallback stateCallback) {
        OPTICK_SET_STATE_CHANGED_CALLBACK(stateCallback);
    }
} // namespace Framework::External::Optick
