/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "errors.h"

#include <mafianet/types.h>

namespace Framework::Networking {
    std::unordered_map<uint8_t, const char *> StartupResultString = {{MafiaNet::RAKNET_ALREADY_STARTED, "Already started"}, {MafiaNet::INVALID_SOCKET_DESCRIPTORS, "Invalid socket descriptors"}, {MafiaNet::INVALID_MAX_CONNECTIONS, "Invalid max connections"},
        {MafiaNet::SOCKET_FAMILY_NOT_SUPPORTED, "Socket family not supported"}, {MafiaNet::SOCKET_PORT_ALREADY_IN_USE, "Port already in use"}, {MafiaNet::SOCKET_FAILED_TO_BIND, "Failed to bind IP address"}, {MafiaNet::SOCKET_FAILED_TEST_SEND, "Failed to test send"},
        {MafiaNet::PORT_CANNOT_BE_ZERO, "Port cannot be zero"}, {MafiaNet::FAILED_TO_CREATE_NETWORK_THREAD, "Failed to create network thread"}, {MafiaNet::COULD_NOT_GENERATE_GUID, "Could not generate GUID"}, {MafiaNet::STARTUP_OTHER_FAILURE, "Unknown failure"}};

    std::unordered_map<uint8_t, const char *> ConnectionAttemptString = {{MafiaNet::INVALID_PARAMETER, "Invalid parameter"}, {MafiaNet::CANNOT_RESOLVE_DOMAIN_NAME, "Cannot resolve domain name"}, {MafiaNet::ALREADY_CONNECTED_TO_ENDPOINT, "Already connected to endpoint"},
        {MafiaNet::CONNECTION_ATTEMPT_ALREADY_IN_PROGRESS, "Connection attempt already in progress"}, {MafiaNet::SECURITY_INITIALIZATION_FAILED, "Security initialization failed"}};
} // namespace Framework::Networking
