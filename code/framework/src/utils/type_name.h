/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string_view>

namespace Framework::Utils {
    // Stable, compiler-independent fully-qualified type name.
    //
    // typeid(T).name() is NOT portable across compilers: MSVC returns a readable
    // decorated name ("class Foo::Bar") while the Itanium ABI (GCC/Clang) returns
    // a mangled name ("N3Foo3BarE"). Anything that uses typeid().name() as a
    // network identity therefore disagrees between, e.g., an MSVC Windows client
    // and a GCC Linux server, so their RPC hashes never match.
    //
    // This derives the name from the compiler's own function signature and
    // normalizes it so MSVC, GCC, and Clang all yield the SAME string for a given
    // type, making it safe to hash as a cross-platform wire identifier.
    template <typename T>
    constexpr std::string_view TypeName() {
#if defined(_MSC_VER)
        // e.g. "...Framework::Utils::TypeName<class Foo::Bar>(void)"
        constexpr std::string_view sig    = __FUNCSIG__;
        constexpr std::string_view prefix = "TypeName<";
        constexpr std::string_view suffix = ">(void)";
#elif defined(__GNUC__) || defined(__clang__)
        // GCC:   "...TypeName() [with T = Foo::Bar; std::string_view = ...]"
        // Clang: "...TypeName() [T = Foo::Bar]"
        constexpr std::string_view sig    = __PRETTY_FUNCTION__;
        constexpr std::string_view prefix = "T = ";
        constexpr std::string_view suffix = "]";
#else
#error "Framework::Utils::TypeName: unsupported compiler (needs __FUNCSIG__ or __PRETTY_FUNCTION__)"
#endif
        const auto begin = sig.find(prefix);
        if (begin == std::string_view::npos)
            return sig; // unknown compiler format — degrade rather than crash
        const auto start = begin + prefix.size();
        auto end         = sig.find(suffix, start);
        if (end == std::string_view::npos)
            end = sig.size();
        // GCC lists further template params after a ';'; cut at the first one.
        const auto semi = sig.find(';', start);
        if (semi != std::string_view::npos && semi < end)
            end = semi;

        std::string_view name = sig.substr(start, end - start);

        // MSVC prefixes class/struct/enum types with an elaborated-type keyword
        // that GCC/Clang omit — strip it so the names match across compilers.
        for (const std::string_view kw : {std::string_view {"class "}, std::string_view {"struct "}, std::string_view {"enum "}}) {
            if (name.size() >= kw.size() && name.substr(0, kw.size()) == kw) {
                name.remove_prefix(kw.size());
                break;
            }
        }
        return name;
    }
} // namespace Framework::Utils
