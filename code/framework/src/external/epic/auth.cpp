/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "auth.h"

#include "utils/string_utils.h"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace Framework::External::Epic {
    namespace {
        // Public "Epic Games Launcher" client credentials — the same ones the launcher itself and
        // tools like Legendary use to mint exchange codes for games the account owns. Not secret.
        constexpr const char *kClientId     = "34a02cf8f4414e29b15921876da36f9a";
        constexpr const char *kClientSecret = "daafbccc737745039dffe53d94fc76cf";

        constexpr const wchar_t *kAuthHost  = L"account-public-service-prod.ol.epicgames.com";
        constexpr const wchar_t *kTokenPath = L"/account/api/oauth/token";
        constexpr const wchar_t *kExchPath  = L"/account/api/oauth/exchange";

        // Browser sign-in: lands on a page whose JSON body contains "authorizationCode".
        constexpr const wchar_t *kLoginUrl =
            L"https://www.epicgames.com/id/login?redirectUrl="
            L"https%3A%2F%2Fwww.epicgames.com%2Fid%2Fapi%2Fredirect%3FclientId%3D"
            L"34a02cf8f4414e29b15921876da36f9a%26responseType%3Dcode";

        std::filesystem::path ExeDir() {
            wchar_t buf[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            return std::filesystem::path(buf).parent_path();
        }

        std::filesystem::path AuthFile() {
            // Per-user secret -> %LOCALAPPDATA%\MafiaHub: survives launcher reinstalls, works under a
            // read-only install, shared across Framework Epic games. Fall back to the exe dir.
            wchar_t localAppData[MAX_PATH] = {};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
                std::error_code ec;
                const std::filesystem::path dir = std::filesystem::path(localAppData) / L"MafiaHub";
                std::filesystem::create_directories(dir, ec);
                if (!ec) {
                    return dir / L"epic_auth.bin";
                }
            }
            return ExeDir() / L"epic_auth.bin";
        }

        std::filesystem::path LogFile() {
            return ExeDir() / L"epic_auth.log"; // diagnostics stay with the other launcher logs
        }

        // Diagnostics only — never logs tokens or the exchange code.
        void Log(const std::string &msg) {
            std::ofstream f(LogFile(), std::ios::app);
            if (f) {
                SYSTEMTIME t;
                GetLocalTime(&t);
                char ts[32];
                std::snprintf(ts, sizeof(ts), "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
                f << ts << msg << "\n";
            }
        }

        std::wstring Widen(const std::string &s) {
            if (s.empty()) {
                return {};
            }
            const int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w(need, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), need);
            return w;
        }

        // One HTTPS request to the Epic account service. Headers are CRLF-joined. Returns false on
        // transport failure; an HTTP error still returns true with outStatus set for the caller.
        bool HttpsRequest(const wchar_t *path, const wchar_t *method, const std::wstring &headers,
                          const std::string &body, std::string &outResp, DWORD &outStatus) {
            outResp.clear();
            outStatus = 0;

            HINTERNET hSession = WinHttpOpen(L"MafiaHubLauncher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) {
                return false;
            }
            WinHttpSetTimeouts(hSession, 5000, 5000, 15000, 30000);

            bool ok            = false;
            HINTERNET hConnect = WinHttpConnect(hSession, kAuthHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (hConnect) {
                HINTERNET hReq = WinHttpOpenRequest(hConnect, method, path, nullptr, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                if (hReq) {
                    if (!headers.empty()) {
                        WinHttpAddRequestHeaders(hReq, headers.c_str(), (DWORD)-1,
                                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                    }
                    const BOOL sent = WinHttpSendRequest(
                        hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                        (DWORD)body.size(), (DWORD)body.size(), 0);
                    if (sent && WinHttpReceiveResponse(hReq, nullptr)) {
                        DWORD status = 0, len = sizeof(status);
                        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                        outStatus = status;

                        for (;;) {
                            DWORD avail = 0;
                            if (!WinHttpQueryDataAvailable(hReq, &avail) || avail == 0) {
                                break;
                            }
                            std::string chunk(avail, '\0');
                            DWORD read = 0;
                            if (!WinHttpReadData(hReq, chunk.data(), avail, &read)) {
                                break;
                            }
                            chunk.resize(read);
                            outResp += chunk;
                        }
                        ok = true;
                    }
                    WinHttpCloseHandle(hReq);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
            return ok;
        }

        std::wstring BasicAuthHeader() {
            const std::string cred = std::string(kClientId) + ":" + kClientSecret;
            return L"Authorization: Basic " + Widen(Utils::StringUtils::Base64Encode(cred)) +
                   L"\r\nContent-Type: application/x-www-form-urlencoded";
        }

        // Fill Tokens from an oauth/token response body. Returns false if it isn't a token grant.
        bool ParseTokens(const std::string &body, Tokens &out) {
            try {
                const auto j = nlohmann::json::parse(body);
                if (!j.contains("access_token")) {
                    return false;
                }
                out.accessToken  = j.value("access_token", std::string {});
                out.refreshToken = j.value("refresh_token", std::string {});
                out.accountId    = j.value("account_id", std::string {});
                out.displayName  = j.value("displayName", std::string {});
                return out.Valid();
            }
            catch (const std::exception &) {
                return false;
            }
        }

        bool TokenGrant(const std::string &formBody, Tokens &out) {
            std::string resp;
            DWORD status = 0;
            if (!HttpsRequest(kTokenPath, L"POST", BasicAuthHeader(), formBody, resp, status)) {
                Log("token: transport failure");
                return false;
            }
            if (status != 200) {
                Log("token: HTTP " + std::to_string(status));
                return false;
            }
            return ParseTokens(resp, out);
        }

        bool RefreshGrant(const std::string &refreshToken, Tokens &out) {
            return TokenGrant("grant_type=refresh_token&token_type=eg1&refresh_token=" + refreshToken, out);
        }

        bool AuthCodeGrant(const std::string &authCode, Tokens &out) {
            return TokenGrant("grant_type=authorization_code&token_type=eg1&code=" + authCode, out);
        }

        // --- refresh-token persistence (DPAPI, tied to the Windows user) ---

        bool SaveRefreshToken(const std::string &rt) {
            if (rt.empty()) {
                return false;
            }
            DATA_BLOB in {(DWORD)rt.size(), (BYTE *)rt.data()};
            DATA_BLOB out {};
            if (!CryptProtectData(&in, L"MafiaHub-Epic", nullptr, nullptr, nullptr, 0, &out)) {
                return false;
            }
            std::ofstream f(AuthFile(), std::ios::binary | std::ios::trunc);
            const bool ok = f && f.write((const char *)out.pbData, out.cbData).good();
            LocalFree(out.pbData);
            return ok;
        }

        bool LoadRefreshToken(std::string &rt) {
            std::ifstream f(AuthFile(), std::ios::binary);
            if (!f) {
                return false;
            }
            const std::string blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (blob.empty()) {
                return false;
            }
            DATA_BLOB in {(DWORD)blob.size(), (BYTE *)blob.data()};
            DATA_BLOB out {};
            if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
                return false;
            }
            rt.assign((const char *)out.pbData, out.cbData);
            LocalFree(out.pbData);
            return !rt.empty();
        }

        // --- interactive sign-in ---

        std::string ReadClipboardText() {
            std::string result;
            if (!OpenClipboard(nullptr)) {
                return result;
            }
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t *w = (const wchar_t *)GlobalLock(h)) {
                    const int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
                    if (need > 0) {
                        result.resize(need - 1);
                        WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), need, nullptr, nullptr);
                    }
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            return result;
        }

        // Accept either the bare authorizationCode or the whole JSON blob the redirect page shows.
        std::string ExtractAuthCode(std::string s) {
            const auto trim = [](std::string &v) {
                const char *ws = " \t\r\n\"'";
                const auto a   = v.find_first_not_of(ws);
                const auto b   = v.find_last_not_of(ws);
                v              = (a == std::string::npos) ? "" : v.substr(a, b - a + 1);
            };
            if (s.find("authorizationCode") != std::string::npos) {
                try {
                    const auto j = nlohmann::json::parse(s);
                    if (j.contains("authorizationCode")) {
                        std::string c = j.value("authorizationCode", std::string {});
                        trim(c);
                        return c;
                    }
                }
                catch (const std::exception &) {
                    // fall through to plain trim
                }
            }
            trim(s);
            return s;
        }

        bool InteractiveSignIn(Tokens &out, const std::wstring &productName) {
            const std::wstring title = productName.empty() ? L"Epic sign-in" : productName + L" — Epic sign-in";

            ShellExecuteW(nullptr, L"open", kLoginUrl, nullptr, nullptr, SW_SHOWNORMAL);
            MessageBoxW(nullptr,
                        L"A browser window was opened to sign in to Epic Games.\n\n"
                        L"After you sign in, a page shows text containing an \"authorizationCode\".\n"
                        L"Select and copy that code (Ctrl+C), then click OK here.",
                        title.c_str(), MB_OK | MB_ICONINFORMATION);

            const std::string code = ExtractAuthCode(ReadClipboardText());
            if (code.empty()) {
                Log("signin: empty authorization code from clipboard");
                return false;
            }
            if (!AuthCodeGrant(code, out)) {
                Log("signin: authorization_code grant failed");
                return false;
            }
            Log("signin: success");
            return true;
        }

        // Ownership-verification token the Epic launcher drops under <install>\.egstore. We take
        // the first .ovt found; empty if the user hasn't launched the game through Epic yet (which
        // is what seeds it). Non-throwing iteration so an unreadable subdir can't abort the launch.
        std::wstring FindOwnershipToken(const std::string &installDir) {
            if (installDir.empty()) {
                return {};
            }
            std::error_code ec;
            const std::filesystem::path egstore = std::filesystem::u8path(installDir) / ".egstore";
            std::filesystem::recursive_directory_iterator it(egstore, ec), end;
            for (; !ec && it != end; it.increment(ec)) {
                std::error_code entryEc;
                if (it->is_regular_file(entryEc) && it->path().extension() == ".ovt") {
                    return it->path().wstring();
                }
            }
            return {};
        }
    } // namespace

    bool EnsureAuthenticated(Tokens &out, const std::wstring &productName) {
        std::string rt;
        if (LoadRefreshToken(rt) && RefreshGrant(rt, out)) {
            Log("auth: refreshed stored token");
            SaveRefreshToken(out.refreshToken); // Epic rotates the refresh token
            return true;
        }

        if (!InteractiveSignIn(out, productName)) {
            return false;
        }
        SaveRefreshToken(out.refreshToken);
        return true;
    }

    bool GetExchangeCode(const Tokens &tokens, std::string &outCode) {
        outCode.clear();
        if (!tokens.Valid()) {
            return false;
        }
        std::string resp;
        DWORD status = 0;
        if (!HttpsRequest(kExchPath, L"GET", L"Authorization: Bearer " + Widen(tokens.accessToken), {}, resp, status)) {
            Log("exchange: transport failure");
            return false;
        }
        if (status != 200) {
            Log("exchange: HTTP " + std::to_string(status));
            return false;
        }
        try {
            const auto j = nlohmann::json::parse(resp);
            outCode      = j.value("code", std::string {});
        }
        catch (const std::exception &) {
            return false;
        }
        return !outCode.empty();
    }

    std::wstring BuildLaunchArgs(const Tokens &tokens, const std::string &exchangeCode,
                                 const std::string &appName, const std::string &sandboxId,
                                 const std::string &installDir) {
        std::wstring a = L" -AUTH_LOGIN=unused -AUTH_PASSWORD=" + Widen(exchangeCode) + L" -AUTH_TYPE=exchangecode";
        if (!appName.empty()) {
            a += L" -epicapp=" + Widen(appName);
        }
        a += L" -epicenv=Prod";

        // Ownership proof — the piece the "use the Epic launcher" gate actually checks.
        const std::wstring ovt = FindOwnershipToken(installDir);
        if (!sandboxId.empty()) {
            a += L" -epicsandboxid=" + Widen(sandboxId);
        }
        if (!ovt.empty()) {
            a += L" -epicovt=\"" + ovt + L"\"";
            Log("ovt: found ownership token");
        }
        else {
            Log("ovt: NONE found under .egstore (launch once via Epic to seed it)");
        }

        if (!tokens.accountId.empty()) {
            a += L" -epicuserid=" + Widen(tokens.accountId);
        }
        if (!tokens.displayName.empty()) {
            a += L" -epicusername=\"" + Widen(tokens.displayName) + L"\"";
        }
        a += L" -epiclocale=en -EpicPortal";
        return a;
    }

    void ClearStoredAuth() {
        std::error_code ec;
        std::filesystem::remove(AuthFile(), ec);
    }
} // namespace Framework::External::Epic
