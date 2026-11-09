/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "native_abi.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Scripting::Api {
    // Where a binding member exists. A language that runs only on the server (C#) gets both.
    enum class Side : uint8_t {
        Shared,
        Server,
    };

    // Which languages a binding member is bound for. A member written against V8 directly (a union-typed setter, a
    // callback taking FunctionCallbackInfo) is JavaScript only; a member a language other than JavaScript needs a
    // different shape for is Native only.
    enum class Target : uint8_t {
        JavaScript = 1,
        Native     = 2,
        All        = JavaScript | Native,
    };

    constexpr bool Includes(Target target, Target what) {
        return (static_cast<uint8_t>(target) & static_cast<uint8_t>(what)) != 0;
    }

    struct NativeParameter {
        std::string name;
        std::string csharpType;
        std::string description;
    };

    // One member of a class as the flat convention exposes it, with what a generator needs to declare it.
    struct NativeMember {
        enum class Kind : uint8_t {
            Method,
            Property,
        };

        Kind kind = Kind::Method;
        std::string name; // the JavaScript name; a generator derives its own spelling from it
        std::string description;
        std::string returnDescription;
        std::string csharpType; // a method's return type, a property's type
        std::vector<NativeParameter> parameters;
        uint32_t id       = 0; // the method, or the property's getter
        uint32_t setterId = 0; // 0 for a read-only property
        Side side         = Side::Shared;
    };

    struct NativeClass {
        std::string name;
        std::string description;
        std::string base; // empty for a root handle type
        std::vector<NativeMember> members;
    };

    /**
     * Every binding member a language other than JavaScript can call, process-wide: the thunks by id, and the classes
     * they belong to, in registration order, for the generated API (docs/scripting_csharp.md).
     *
     * A member's id is the CRC32 of "<Class>.<member>" ("=" appended for a property's setter), so a generated API
     * resolves a thunk by a constant it was generated with; a binding that no longer exists fails that lookup at load
     * instead of calling something else.
     */
    class NativeCatalog final {
      public:
        static NativeCatalog &Get();

        static uint32_t IdOf(const std::string &key);

        // A class is registered as soon as its binding is constructed and filled in afterwards; the pointer must stay
        // valid for the process (bindings are function-local statics).
        void AddClass(NativeClass *nativeClass);

        // Throws std::logic_error when another member already took the id.
        void AddThunk(uint32_t id, const std::string &key, NativeThunk thunk);

        // nullptr for an id nothing registered.
        NativeThunk Find(uint32_t id) const;

        std::vector<const NativeClass *> GetClasses() const;

        // The C# API for these classes, bases before the classes extending them: the Framework's is the generated
        // file managed/MafiaHub.Framework.Scripting/Generated/Bindings.g.cs.
        static void WriteCSharp(std::ostream &out, const std::vector<const NativeClass *> &classes);

      private:
        NativeCatalog() = default;

        mutable std::mutex _mutex;
        std::vector<NativeClass *> _classes;
        std::unordered_map<uint32_t, std::pair<std::string, NativeThunk>> _thunks;
    };
} // namespace Framework::Scripting::Api
