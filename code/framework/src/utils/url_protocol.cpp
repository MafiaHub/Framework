/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "url_protocol.h"

#include <algorithm>
#include <cwctype>

namespace Framework::Utils::UrlProtocol {
    std::optional<std::wstring> ExtractLaunchUrl(const std::wstring &scheme, const std::wstring &commandLine, size_t maxLength) {
        if (scheme.empty()) {
            return std::nullopt;
        }

        const auto toLower = [](std::wstring value) {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
                return static_cast<wchar_t>(std::towlower(c));
            });
            return value;
        };

        // Case-insensitive match, but slice from the original.
        const std::wstring needle = toLower(scheme) + L"://";
        const auto position       = toLower(commandLine).find(needle);
        if (position == std::wstring::npos) {
            return std::nullopt;
        }

        std::wstring url = commandLine.substr(position);

        // Drop trailing whitespace and the "%1" closing quote.
        while (!url.empty() && std::iswspace(url.back())) {
            url.pop_back();
        }
        if (!url.empty() && url.back() == L'"') {
            url.pop_back();
        }
        while (!url.empty() && std::iswspace(url.back())) {
            url.pop_back();
        }

        if (url.empty() || url.size() > maxLength) {
            return std::nullopt;
        }

        // Reject injection chars we won't interpret.
        for (const wchar_t c : url) {
            if (c == L'"' || c == L'\\' || c < 0x20) {
                return std::nullopt;
            }
        }
        if (url.find(L"..") != std::wstring::npos) {
            return std::nullopt;
        }

        return url;
    }
} // namespace Framework::Utils::UrlProtocol
