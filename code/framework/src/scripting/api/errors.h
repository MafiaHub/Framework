/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace Framework::Scripting::Api {
    // What kind of failure a binding reports, independent of the language that called it. Each language maps the
    // kind to its own error (scripting/builtins/README.md has the table). A stale receiver is not an error kind: it
    // is a silent no-op, decided before the binding runs.
    enum class ErrorKind : int32_t {
        // Wrong argument count, type or shape: a TypeError in JavaScript, an ArgumentException in C#.
        Argument = 1,

        // A valid call the world cannot satisfy (subsystem missing, limit reached, no owning connection): an Error in
        // JavaScript, an InvalidOperationException in C#.
        State = 2,
    };

    // Thrown by a binding's C++ body; the language backend that made the call turns it into its own error.
    class ScriptError final: public std::runtime_error {
      public:
        ScriptError(ErrorKind kind, const std::string &message): std::runtime_error(message), _kind(kind) {}

        ErrorKind GetKind() const {
            return _kind;
        }

      private:
        ErrorKind _kind;
    };

    [[noreturn]] inline void ThrowArgumentError(const std::string &message) {
        throw ScriptError(ErrorKind::Argument, message);
    }

    [[noreturn]] inline void ThrowStateError(const std::string &message) {
        throw ScriptError(ErrorKind::State, message);
    }
} // namespace Framework::Scripting::Api
