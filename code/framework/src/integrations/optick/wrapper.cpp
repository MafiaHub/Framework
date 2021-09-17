#include "wrapper.h"

namespace Framework::Integrations {
    void Optick::StartCapture() {
        OPTICK_START_CAPTURE();
    }

    void Optick::StopCapture() {
        OPTICK_STOP_CAPTURE();
    }

    void Optick::Dump(const char *path) {
        OPTICK_SAVE_CAPTURE(path);
    }

    void Optick::Update() {
        OPTICK_UPDATE();
    }

    void Optick::Shutdown() {
        OPTICK_SHUTDOWN();
    }

    void Optick::SetAllocator(::Optick::AllocateFn allocateFn, ::Optick::DeallocateFn deallocateFn,
                              ::Optick::InitThreadCb initThreadCb) {
        OPTICK_SET_MEMORY_ALLOCATOR(allocateFn, deallocateFn, initThreadCb);
    }

    void Optick::SetStateChangedCallback(::Optick::StateCallback stateCallback) {
        OPTICK_SET_STATE_CHANGED_CALLBACK(stateCallback);
    }
} // namespace Framework::Integrations
