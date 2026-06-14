/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/hashing.h"
#include "utils/type_name.h"

#include <string_view>

// Fixture types at namespace scope so their names are predictable. The exact
// expected strings below are the cross-compiler contract: MSVC, GCC, and Clang
// must all produce the SAME name for a given type (this is what makes the RPC
// hash portable). If a compiler ever diverges, the equality checks fail on that
// platform's CI build.
namespace Framework::Tests::TypeNameFixture {
    struct Alpha {};
    class Beta {};
    enum Gamma { GammaValue };
    namespace Inner {
        struct Delta {};
    }
} // namespace Framework::Tests::TypeNameFixture

MODULE(type_name, {
    using Framework::Utils::TypeName;
    namespace Fx = Framework::Tests::TypeNameFixture;

    IT("returns the fully-qualified name, identical across compilers", {
        EQUALS(TypeName<Fx::Alpha>() == std::string_view("Framework::Tests::TypeNameFixture::Alpha"), true);
        EQUALS(TypeName<Fx::Beta>() == std::string_view("Framework::Tests::TypeNameFixture::Beta"), true);
    });

    IT("resolves nested namespaces", {
        EQUALS(TypeName<Fx::Inner::Delta>() == std::string_view("Framework::Tests::TypeNameFixture::Inner::Delta"), true);
    });

    IT("strips the class/struct/enum keyword MSVC prepends", {
        // GCC/Clang never emit these; MSVC does, and TypeName must remove them
        // so all three agree.
        EQUALS(TypeName<Fx::Alpha>().rfind("struct ", 0) == 0, false);
        EQUALS(TypeName<Fx::Beta>().rfind("class ", 0) == 0, false);
        EQUALS(TypeName<Fx::Gamma>().rfind("enum ", 0) == 0, false);
    });

    IT("handles primitive types", {
        EQUALS(TypeName<int>() == std::string_view("int"), true);
    });

    IT("is stable across repeated calls", {
        EQUALS(TypeName<Fx::Alpha>() == TypeName<Fx::Alpha>(), true);
    });

    IT("gives distinct types distinct names and CRC32 hashes", {
        EQUALS(TypeName<Fx::Alpha>() == TypeName<Fx::Beta>(), false);

        const auto ha = Framework::Utils::Hashing::CalculateCRC32(std::string(TypeName<Fx::Alpha>()));
        const auto hb = Framework::Utils::Hashing::CalculateCRC32(std::string(TypeName<Fx::Beta>()));
        EQUALS(ha == hb, false);
    });
});
