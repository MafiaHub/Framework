/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "protocol.h"

namespace Framework::GUI::Remote::Protocol {
    bool ParseHeader(const uint8_t *bytes, MessageHeader &header, uint32_t maxPayload) {
        std::memcpy(&header.payloadSize, bytes, sizeof(header.payloadSize));
        std::memcpy(&header.type, bytes + 4, sizeof(header.type));
        std::memcpy(&header.reserved, bytes + 6, sizeof(header.reserved));
        return header.payloadSize <= maxPayload && header.reserved == 0;
    }
} // namespace Framework::GUI::Remote::Protocol
