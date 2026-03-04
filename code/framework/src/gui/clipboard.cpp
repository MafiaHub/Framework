/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "clipboard.h"

#include <string>
#include <utils/safe_win32.h>

namespace Framework::GUI {
    void SystemClipboard::Clear() {
        OpenClipboard(nullptr);
        EmptyClipboard();
        CloseClipboard();
    }

    std::string SystemClipboard::ReadPlainText() {
        if (!OpenClipboard(nullptr))
            return "";

        const HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData == nullptr) {
            CloseClipboard();
            return "";
        }

        const auto pszText = static_cast<char *>(GlobalLock(hData));
        if (pszText == nullptr) {
            CloseClipboard();
            return "";
        }

        const std::string text(pszText);

        GlobalUnlock(hData);
        CloseClipboard();

        return text;
    }

    void SystemClipboard::WritePlainText(std::string_view text) {
        if (!OpenClipboard(nullptr)) {
            return;
        }
        EmptyClipboard();
        const size_t size   = text.length() + 1;
        HGLOBAL hClipboardData = GlobalAlloc(GMEM_DDESHARE, size);
        const auto pchData  = static_cast<char *>(GlobalLock(hClipboardData));
        memcpy(pchData, text.data(), text.length());
        pchData[text.length()] = '\0';
        GlobalUnlock(hClipboardData);
        SetClipboardData(CF_TEXT, hClipboardData);
        CloseClipboard();
    }
} // namespace Framework::GUI
