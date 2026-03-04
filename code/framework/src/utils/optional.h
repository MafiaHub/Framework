/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Utils {
    template <typename T>
    class Optional {
      private:
        T _value {};
        bool _hasValue = false;

      public:
        Optional() = default;
        Optional(T value): _value(value), _hasValue(true) {};

        Optional(const Optional &other) {
            _value    = other._value;
            _hasValue = other._hasValue;
        }

        Optional &operator=(const Optional &other) {
            _value    = other._value;
            _hasValue = other._hasValue;
            return *this;
        }

        inline bool HasValue() const {
            return _hasValue;
        }

        inline T Value() const {
            return _value;
        }

        inline const T &RefValue() const {
            return _value;
        }

        inline void Clear() {
            _value    = T {};
            _hasValue = false;
        }

        inline T operator()() const {
            return _value;
        }

        inline void operator=(T value) {
            _value    = value;
            _hasValue = true;
        }
    };
} // namespace Framework::Utils
