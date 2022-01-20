/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Utils {
    template <typename T>
    class Watcher {
      private:
        T _value{};
        bool _hasChanged = false;
      public:
        Watcher() = default;
        Watcher(T value) : _value(value), _hasChanged(true) {};

        inline bool HasChanged() const {
            return _hasChanged;
        }

        inline T Value() const {
            return _value;
        }

        inline void Changed(T value) {
            _value = value;
            _hasChanged = true;
        }

        inline void Unchanged() {
            _hasChanged = false;
        }

        inline T operator()() const {
            return _value;
        }

        inline void operator=(T value) {
            _hasChanged = (_value != value);
            _value = value;
        }
    };
} // namespace Framework::Utils
