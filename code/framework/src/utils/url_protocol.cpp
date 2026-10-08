/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "url_protocol.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cwctype>
#include <system_error>

#ifdef _WIN32
#include "safe_win32.h"
#endif

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

    namespace {
        bool IsAcceptable(char value) {
            return static_cast<unsigned char>(value) >= 0x20 && value != '"' && value != '\\';
        }

        bool IsAsciiLetter(wchar_t value) {
            return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z');
        }

        std::wstring Quoted(const std::filesystem::path &path) {
            return L'"' + path.wstring() + L'"';
        }

        std::optional<std::string> DecodeComponent(std::string_view value) {
            std::string decoded;
            decoded.reserve(value.size());

            for (size_t i = 0; i < value.size(); ++i) {
                char character = value[i];
                if (character == '+') {
                    character = ' ';
                }
                else if (character == '%') {
                    if (value.size() - i < 3) {
                        return std::nullopt;
                    }

                    const char *first = value.data() + i + 1;
                    const char *last  = first + 2;
                    uint8_t byte      = 0;
                    if (const auto [stopped, error] = std::from_chars(first, last, byte, 16); error != std::errc {} || stopped != last) {
                        return std::nullopt;
                    }

                    character = static_cast<char>(byte);
                    i += 2;
                }

                if (!IsAcceptable(character)) {
                    return std::nullopt;
                }
                decoded.push_back(character);
            }

            return decoded;
        }
    } // namespace

    std::optional<std::string_view> ParsedUrl::Query(std::string_view key) const {
        const auto entry = query.find(key);
        if (entry == query.end()) {
            return std::nullopt;
        }
        return entry->second;
    }

    std::optional<ParsedUrl> Parse(std::string_view scheme, std::string_view url) {
        const auto sameIgnoringCase = [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
        };

        if (scheme.empty() || url.size() <= scheme.size() + 3) {
            return std::nullopt;
        }
        if (!std::equal(scheme.begin(), scheme.end(), url.begin(), sameIgnoringCase) || url.substr(scheme.size(), 3) != "://") {
            return std::nullopt;
        }

        const std::string_view body = url.substr(scheme.size() + 3);
        std::string_view authority  = body.substr(0, body.find('#'));

        std::string_view query;
        if (const size_t separator = authority.find('?'); separator != std::string_view::npos) {
            query     = authority.substr(separator + 1);
            authority = authority.substr(0, separator);
        }

        while (!authority.empty() && authority.back() == '/') {
            authority.remove_suffix(1);
        }

        ParsedUrl parsed;
        if (const size_t separator = authority.rfind(':'); separator != std::string_view::npos) {
            const std::string_view port = authority.substr(separator + 1);
            const char *last            = port.data() + port.size();
            uint16_t value              = 0;
            // uint16_t is the upper bound, so zero is all that is left to reject.
            if (const auto [stopped, error] = std::from_chars(port.data(), last, value); error != std::errc {} || stopped != last || value == 0) {
                return std::nullopt;
            }

            parsed.port = value;
            authority   = authority.substr(0, separator);
        }

        if (authority.empty() || !std::all_of(authority.begin(), authority.end(), IsAcceptable)) {
            return std::nullopt;
        }
        parsed.host = authority;

        while (!query.empty()) {
            const std::string_view pair = query.substr(0, query.find('&'));
            query.remove_prefix(std::min(pair.size() + 1, query.size()));

            const size_t separator = pair.find('=');
            if (separator == std::string_view::npos) {
                continue;
            }

            std::optional<std::string> key   = DecodeComponent(pair.substr(0, separator));
            std::optional<std::string> value = DecodeComponent(pair.substr(separator + 1));
            if (!key || !value) {
                return std::nullopt;
            }
            parsed.query.insert_or_assign(std::move(*key), std::move(*value));
        }

        return parsed;
    }

    bool IsValidScheme(std::wstring_view scheme) {
        if (scheme.empty() || !IsAsciiLetter(scheme.front())) {
            return false;
        }

        return std::all_of(scheme.begin(), scheme.end(), [](wchar_t value) {
            return IsAsciiLetter(value) || (value >= L'0' && value <= L'9') || value == L'+' || value == L'-' || value == L'.';
        });
    }

    std::wstring BuildOpenCommand(const std::filesystem::path &executablePath) {
        return Quoted(executablePath) + L" \"%1\"";
    }

#ifdef _WIN32
    namespace {
        constexpr const wchar_t *kClassesRoot = L"Software\\Classes\\";

        // nullopt when the value is absent or is not a string. A name of nullptr reads the key's own
        // default value, which is where the shell keeps three of the four we write.
        std::optional<std::wstring> ReadString(const std::wstring &subKey, const wchar_t *name) {
            DWORD bytes = 0;
            if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS) {
                return std::nullopt;
            }

            // RRF_RT_REG_SZ makes the reported size cover the terminator, so this is the whole value.
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) {
                return std::nullopt;
            }

            value.resize(wcslen(value.c_str()));
            return value;
        }

        // A value that already reads back as what we want is left alone, so the ordinary call - a
        // launcher re-registering itself on every start - creates no key and writes nothing.
        bool WriteString(const std::wstring &subKey, const wchar_t *name, const std::wstring &value) {
            if (ReadString(subKey, name) == value) {
                return true;
            }

            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
                return false;
            }

            const auto bytes  = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            const auto status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), bytes);
            RegCloseKey(key);
            return status == ERROR_SUCCESS;
        }
    } // namespace

    bool Register(std::wstring_view scheme, const std::wstring &description, const std::filesystem::path &executablePath) {
        if (!IsValidScheme(scheme) || executablePath.empty()) {
            return false;
        }

        const std::wstring root = kClassesRoot + std::wstring(scheme);

        // "URL Protocol" is the marker ShellExecute looks for: the name is what matters and the value
        // is empty by convention. The icon is the launcher's own, so a browser's "open with" prompt
        // shows the mod rather than a blank sheet.
        return WriteString(root, nullptr, description) && WriteString(root, L"URL Protocol", L"") && WriteString(root + L"\\DefaultIcon", nullptr, Quoted(executablePath) + L",0")
            && WriteString(root + L"\\shell\\open\\command", nullptr, BuildOpenCommand(executablePath));
    }

    bool Unregister(std::wstring_view scheme) {
        if (!IsValidScheme(scheme)) {
            return false;
        }

        const std::wstring root = kClassesRoot + std::wstring(scheme);
        const auto status       = RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }
#endif
} // namespace Framework::Utils::UrlProtocol
