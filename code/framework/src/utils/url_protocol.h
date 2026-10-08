/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Framework::Utils::UrlProtocol {
    // Pulls the "<scheme>://..." token from a raw command line (not argv: ShellExecute may split it).
    // nullopt if absent, over maxLength, or holding a quote/backslash/control char/".."; no decoding.
    std::optional<std::wstring> ExtractLaunchUrl(const std::wstring &scheme, const std::wstring &commandLine, size_t maxLength = 2048);

    // A "<scheme>://host[:port][/][?key=value&...][#fragment]" deep link, split into its parts.
    struct ParsedUrl {
        std::string host;
        std::optional<uint16_t> port;
        std::map<std::string, std::string, std::less<>> query;

        [[nodiscard]] std::optional<std::string_view> Query(std::string_view key) const;
    };

    // scheme carries no "://" and matches case-insensitively; keys and values are percent-decoded
    // ("+" is a space). nullopt on a scheme mismatch, an empty host, a port outside [1, 65535], a
    // bad escape, or a character ExtractLaunchUrl would have rejected.
    std::optional<ParsedUrl> Parse(std::string_view scheme, std::string_view url);

    // True for a scheme the shell will take as a key name: an ASCII letter followed by letters,
    // digits, "+", "-" or ".". The scheme becomes part of a registry path, so a caller-supplied
    // separator has to be refused before it gets there.
    bool IsValidScheme(std::wstring_view scheme);

    // The shell\open\command line a registration installs: the quoted executable followed by the
    // URL the shell substitutes for "%1". No flag of its own, because ExtractLaunchUrl finds the
    // "<scheme>://" token wherever on the command line it lands.
    std::wstring BuildOpenCommand(const std::filesystem::path &executablePath);

#ifdef _WIN32
    // Points "<scheme>://" at executablePath for the user who is playing, under
    // HKCU\Software\Classes\<scheme>: no elevation, nothing written for any other account, and
    // reversible with Unregister. Values that already hold what we want are left alone, so a
    // launcher can call this on every start - it follows itself across moves and updates and
    // touches the registry only when something actually changed.
    //
    // description is the string the shell shows for the scheme, e.g. L"URL:MafiaMP link".
    // False when the scheme is invalid, the path is empty, or a key could not be written.
    bool Register(std::wstring_view scheme, const std::wstring &description, const std::filesystem::path &executablePath);

    // Removes HKCU\Software\Classes\<scheme> and everything under it. True once the scheme is
    // gone, including when it was never registered.
    bool Unregister(std::wstring_view scheme);
#endif
} // namespace Framework::Utils::UrlProtocol
