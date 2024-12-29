#include "clipboard.h"

#include <string>
#include <utils/safe_win32.h>

namespace Framework::GUI {
    void SystemClipboard::Clear() {
        // win32 clear clipboard
        OpenClipboard(nullptr);
        EmptyClipboard();
        CloseClipboard();
    }
    ultralight::String SystemClipboard::ReadPlainText() {
        // Try to open the clipboard
        if (!OpenClipboard(nullptr))
            return ""; // If opening clipboard failed, return an empty string

        // Get the clipboard data in CF_TEXT format
        const HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData == nullptr) {
            CloseClipboard(); // Close the clipboard if getting data failed
            return "";
        }

        // Lock the handle to get the actual text pointer
        const auto pszText = static_cast<char *>(GlobalLock(hData));
        if (pszText == nullptr) {
            CloseClipboard(); // Close the clipboard if lock failed
            return "";
        }

        // Copy the text data to a std::string instance
        const std::string text(pszText);

        // Unlock the handle and close the clipboard
        GlobalUnlock(hData);
        CloseClipboard();

        return text.c_str();
    }
    void SystemClipboard::WritePlainText(const ultralight::String &text) {
        if (!OpenClipboard(nullptr)) {
            return;
        }
        EmptyClipboard();
        HGLOBAL hClipboardData;
        const size_t size  = text.utf8().length() + 1;
        hClipboardData     = GlobalAlloc(GMEM_DDESHARE, size);
        const auto pchData = static_cast<char *>(GlobalLock(hClipboardData));
        memcpy(pchData, text.utf8().data(), size);
        GlobalUnlock(hClipboardData);
        SetClipboardData(CF_TEXT, hClipboardData);
        CloseClipboard();
    }
} // namespace Framework::GUI
