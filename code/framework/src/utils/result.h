/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
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
        ResultType _message {};
        ErrorType _errorCode {};

      public:
        Result(ErrorType error, const ResultType &message = {}): _message(message), _errorCode(error) {} // NOLINT(google-explicit-constructor)

        inline ErrorType GetError() const noexcept {
            return _errorCode;
        }

        inline const ResultType &Unwrap() const noexcept {
            return _message;
        }

        inline bool Equals(const ErrorType &rhs) const noexcept {
            return _errorCode == rhs;
        }

        explicit operator bool() const noexcept {
            return _errorCode == ErrorType {};
        }

        static Result Ok(const ResultType &message = {}) {
            return Result(ErrorType {}, message);
        }
    };
} // namespace Framework::Utils
