/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Utils {
    template <typename ResultType, typename ErrorType = uint32_t>
    class Result {
    private:
        ResultType _message{};
        ErrorType _errorCode{};
    public:
        Result(ErrorType error, const ResultType &message = {}) : _errorCode(error), _message(message) {}

        inline const ErrorType GetError() const {
            return _errorCode;
        }

        inline const ResultType &Unwrap() const {
            return _message;
        }

        inline bool Equals(const ErrorType &rhs) const {
            return _errorCode == rhs;
        }
    };
} // namespace Framework::Utils
