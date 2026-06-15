/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <utility>

namespace Framework::Utils {
    // A value-or-error container. ErrorType's default-constructed value is the success sentinel
    // (ErrorType{} == ok), so it composes with both plain code enums and Framework::Error. Use the
    // void specialization below for fallible operations that carry no value (e.g. Init).
    template <typename ResultType, typename ErrorType = uint32_t>
    class Result {
      private:
        ResultType _value {};
        ErrorType _errorCode {};

      public:
        Result(ErrorType error, ResultType value = {}): _value(std::move(value)), _errorCode(std::move(error)) {} // NOLINT(google-explicit-constructor)

        inline const ErrorType &GetError() const noexcept {
            return _errorCode;
        }

        inline const ResultType &GetValue() const noexcept {
            return _value;
        }

        inline bool IsOk() const noexcept {
            return _errorCode == ErrorType {};
        }

        inline bool Equals(const ErrorType &rhs) const noexcept {
            return _errorCode == rhs;
        }

        explicit operator bool() const noexcept {
            return IsOk();
        }

        // fn(value) -> U; carries the error through unchanged on failure.
        template <typename Fn>
        auto Map(Fn &&fn) const -> Result<decltype(fn(std::declval<ResultType>())), ErrorType> {
            using Mapped = Result<decltype(fn(std::declval<ResultType>())), ErrorType>;
            if (!IsOk()) {
                return Mapped(_errorCode);
            }
            return Mapped::Ok(fn(_value));
        }

        // fn(value) -> Result<U, ErrorType>; chains a fallible step, short-circuiting on failure.
        template <typename Fn>
        auto AndThen(Fn &&fn) const -> decltype(fn(std::declval<ResultType>())) {
            using Next = decltype(fn(std::declval<ResultType>()));
            if (!IsOk()) {
                return Next(_errorCode);
            }
            return fn(_value);
        }

        static Result Ok(ResultType value = {}) {
            return Result(ErrorType {}, std::move(value));
        }

        static Result Err(ErrorType error) {
            return Result(std::move(error));
        }
    };

    // Valueless result for fallible operations that only succeed or fail with an error.
    template <typename ErrorType>
    class Result<void, ErrorType> {
      private:
        ErrorType _errorCode {};

      public:
        Result() = default;
        Result(ErrorType error): _errorCode(std::move(error)) {} // NOLINT(google-explicit-constructor)

        inline const ErrorType &GetError() const noexcept {
            return _errorCode;
        }

        inline bool IsOk() const noexcept {
            return _errorCode == ErrorType {};
        }

        inline bool Equals(const ErrorType &rhs) const noexcept {
            return _errorCode == rhs;
        }

        explicit operator bool() const noexcept {
            return IsOk();
        }

        // fn() -> Result<U, ErrorType>; runs the next step only on success.
        template <typename Fn>
        auto AndThen(Fn &&fn) const -> decltype(fn()) {
            using Next = decltype(fn());
            if (!IsOk()) {
                return Next(_errorCode);
            }
            return fn();
        }

        static Result Ok() {
            return Result();
        }

        static Result Err(ErrorType error) {
            return Result(std::move(error));
        }
    };
} // namespace Framework::Utils
