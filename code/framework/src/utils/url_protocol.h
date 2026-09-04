/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
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
} // namespace Framework::Utils::UrlProtocol
