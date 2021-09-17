/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Integrations::Server {
    enum class ServerError { SERVER_NONE, SERVER_WEBSERVER_INIT_FAILED, SERVER_NETWORKING_INIT_FAILED, SERVER_SCRIPTING_INIT_FAILED };
}
