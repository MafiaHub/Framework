/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "errors.h"

#include <slikenet/types.h>

namespace Framework::Networking {
    std::unordered_map<uint8_t, const char *> StartupResultString = {{SLNet::RAKNET_ALREADY_STARTED, "Already started"},
        {SLNet::INVALID_SOCKET_DESCRIPTORS, "Invalid socket descriptors"}, {SLNet::INVALID_MAX_CONNECTIONS, "Invalid max connections"},
        {SLNet::SOCKET_FAMILY_NOT_SUPPORTED, "Socket family not supported"}, {SLNet::SOCKET_PORT_ALREADY_IN_USE, "Port already in use"},
        {SLNet::SOCKET_FAILED_TO_BIND, "Failed to bind IP address"}, {SLNet::SOCKET_FAILED_TEST_SEND, "Failed to test send"}, {SLNet::PORT_CANNOT_BE_ZERO, "Port cannot be zero"},
        {SLNet::FAILED_TO_CREATE_NETWORK_THREAD, "Failed to create network thread"}, {SLNet::COULD_NOT_GENERATE_GUID, "Could not generate GUID"},
        {SLNet::STARTUP_OTHER_FAILURE, "Unknown failure"}};

    std::unordered_map<uint8_t, const char *> ConnectionAttemptString = {{SLNet::INVALID_PARAMETER, "Invalid paremeter"},
        {SLNet::CANNOT_RESOLVE_DOMAIN_NAME, "Cannot resolve domain name"}, {SLNet::ALREADY_CONNECTED_TO_ENDPOINT, "Already connected to endpoint"},
        {SLNet::CONNECTION_ATTEMPT_ALREADY_IN_PROGRESS, "Connection attempt already in progress"}, {SLNet::SECURITY_INITIALIZATION_FAILED, "Security initialization failed"}};
}; // namespace Framework::Networking
