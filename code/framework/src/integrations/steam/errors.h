#pragma once

namespace Framework::Integrations::Steam {
    enum WrapperError {
        WRAPPER_ERROR_NONE,
        WRAPPER_ERROR_STEAM_NOT_RUNNING,
        WRAPPER_ERROR_INIT_STEAM,
        WRAPPER_ERROR_CONTEXT_CREATE,
        WRAPPER_ERROR_CONTEXT_INIT
    };
}
