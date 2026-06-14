/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace Framework {
    // The framework-wide failure type carried by Utils::Result. A default-constructed Error (code 0,
    // empty message) is the success sentinel; any failure carries a human-readable message and an
    // optional subsystem-specific code. Construct failures with a message so the cause survives past
    // the log.
    struct Error {
        std::string message;
        int32_t code = 0;

        Error() = default;
        Error(std::string msg, int32_t errorCode = 0): message(std::move(msg)), code(errorCode) {} // NOLINT(google-explicit-constructor)

        bool operator==(const Error &other) const noexcept {
            return code == other.code && message == other.message;
        }
        bool operator!=(const Error &other) const noexcept {
            return !(*this == other);
        }
    };
} // namespace Framework
