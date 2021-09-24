#pragma once

namespace Framework::Integrations::Server {
    enum class ServerError {
        SERVER_NONE,
        SERVER_WEBSERVER_INIT_FAILED,
        SERVER_SCRIPTING_INIT_FAILED
    };
}