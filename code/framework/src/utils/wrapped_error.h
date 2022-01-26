/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>
#include <cstdint>

namespace Framework::Utils {
    template <typename T = uint32_t>
    class WrappedError {
    private:
        std::string _message;
        T _errorCode;
    public:
        WrappedError(T error, const std::string &message = "") : _errorCode(error), _message(message) {}

        inline const T GetError() const {
            return _errorCode;
        }

        inline const std::string &GetMsg() const {
            return _message;
        }

        inline bool Equals(const T &rhs) const {
            return _errorCode == rhs;
        }
    };
} // namespace Framework::Utils
