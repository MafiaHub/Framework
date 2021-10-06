/*
 * MafiaHub OSS license
 * Copyright (c) 2021 MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Networking {
    enum ServerError {
        SERVER_NONE,
        SERVER_PEER_FAILED,
        SERVER_PEER_NULL
    };

    enum ClientError {
        CLIENT_NONE,
        CLIENT_PEER_FAILED,
        CLIENT_PEER_NULL,
        CLIENT_ALREADY_CONNECTED,
        CLIENT_CONNECT_FAILED
    };
};