/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "string_utils.h"

#ifdef _WIN32

#include "safe_win32.h"

namespace Framework::Utils::StringUtils {
    namespace {
        std::string Narrow(UINT codePage, std::wstring_view wide) {
            if (wide.empty()) {
                return {};
            }
            const int length = static_cast<int>(wide.size());
            const int size   = WideCharToMultiByte(codePage, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
            if (size <= 0) {
                return {};
            }
            std::string out(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(codePage, 0, wide.data(), length, out.data(), size, nullptr, nullptr);
            return out;
        }

        std::wstring Widen(UINT codePage, std::string_view narrow) {
            if (narrow.empty()) {
                return {};
            }
            const int length = static_cast<int>(narrow.size());
            const int size   = MultiByteToWideChar(codePage, 0, narrow.data(), length, nullptr, 0);
            if (size <= 0) {
                return {};
            }
            std::wstring out(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(codePage, 0, narrow.data(), length, out.data(), size);
            return out;
        }
    } // namespace

    std::string WideToUTF8(std::wstring_view wide) {
        return Narrow(CP_UTF8, wide);
    }

    std::wstring UTF8ToWide(std::string_view utf8) {
        return Widen(CP_UTF8, utf8);
    }

    std::string WideToACP(std::wstring_view wide) {
        return Narrow(CP_ACP, wide);
    }

    std::wstring ACPToWide(std::string_view acp) {
        return Widen(CP_ACP, acp);
    }
} // namespace Framework::Utils::StringUtils

#endif
