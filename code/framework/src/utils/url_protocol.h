/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <optional>
#include <string>

namespace Framework::Utils::UrlProtocol {
    // Pulls the "<scheme>://..." token from a raw command line (not argv: ShellExecute may split it).
    // nullopt if absent, over maxLength, or holding a quote/backslash/control char/".."; no decoding.
    std::optional<std::wstring> ExtractLaunchUrl(const std::wstring &scheme, const std::wstring &commandLine, size_t maxLength = 2048);
} // namespace Framework::Utils::UrlProtocol
