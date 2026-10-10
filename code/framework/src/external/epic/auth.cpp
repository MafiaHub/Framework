/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "auth.h"
#include "account_proof.h"

#include "logging/logger.h"
#include "utils/string_utils.h"

// Its own block so include sorting can't move it: wincrypt.h and winhttp.h need it first.
#include <windows.h>

#include <wincrypt.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace Framework::External::Epic {
    namespace {
        // Public "Epic Games Launcher" client credentials — the same ones the launcher itself and
        // tools like Legendary use to mint exchange codes for games the account owns. Not secret.
        constexpr const char *kClientId     = "34a02cf8f4414e29b15921876da36f9a";
        constexpr const char *kClientSecret = "daafbccc737745039dffe53d94fc76cf";

        constexpr const wchar_t *kAuthHost  = L"account-public-service-prod.ol.epicgames.com";
        constexpr const wchar_t *kTokenPath = L"/account/api/oauth/token";
        constexpr const wchar_t *kExchPath  = L"/account/api/oauth/exchange";
        constexpr const wchar_t *kEcomHost  = L"ecommerceintegration-public-service-ecomprod02.ol.epicgames.com";

        // Browser sign-in: lands on a page whose JSON body contains "authorizationCode".
        constexpr const wchar_t *kLoginUrl = L"https://www.epicgames.com/id/login?redirectUrl="
                                             L"https%3A%2F%2Fwww.epicgames.com%2Fid%2Fapi%2Fredirect%3FclientId%3D"
                                             L"34a02cf8f4414e29b15921876da36f9a%26responseType%3Dcode";

        // Diagnostics only — never log tokens, codes or the ownership token.
        auto Logger() {
            return Logging::GetLogger(FRAMEWORK_INNER_LAUNCHER);
        }

        std::filesystem::path ExeDir() {
            wchar_t buf[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            return std::filesystem::path(buf).parent_path();
        }

        // Per-user secrets -> %LOCALAPPDATA%\MafiaHub: survives launcher reinstalls, works under a
        // read-only install, shared across Framework Epic games. Fall back to the exe dir.
        std::filesystem::path DataDir() {
            wchar_t localAppData[MAX_PATH] = {};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
                std::error_code ec;
                const std::filesystem::path dir = std::filesystem::path(localAppData) / L"MafiaHub";
                std::filesystem::create_directories(dir, ec);
                if (!ec) {
                    return dir;
                }
            }
            return ExeDir();
        }

        std::filesystem::path AuthFile() {
            return DataDir() / L"epic_auth.bin";
        }

        bool IsAsciiAlnum(unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }

        // An application/x-www-form-urlencoded value: everything but RFC 3986 unreserved bytes escaped.
        std::string FormEncode(std::string_view value) {
            constexpr char kHex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(value.size() * 3);
            for (const unsigned char c : value) {
                if (IsAsciiAlnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                    out += static_cast<char>(c);
                }
                else {
                    out += '%';
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                }
            }
            return out;
        }

        // Epic catalog and account ids are alphanumeric; they also name files and URL paths here.
        bool IsEpicId(std::string_view value) {
            return !value.empty() && value.size() <= 64 && std::ranges::all_of(value, [](char c) {
                return IsAsciiAlnum(static_cast<unsigned char>(c));
            });
        }

        // Quoted on the game's command line: a '"' would end the argument early, and trailing
        // backslashes would escape the closing quote.
        std::string QuotableArgument(std::string value) {
            std::erase(value, '"');
            while (!value.empty() && value.back() == '\\') {
                value.pop_back();
            }
            return value;
        }

        // One session for the process: WinHTTP sessions are thread-safe and pool connections.
        // Never closed, so no request can outlive it during shutdown.
        HINTERNET Session() {
            static const HINTERNET session = [] {
                HINTERNET handle = WinHttpOpen(L"MafiaHubLauncher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
                if (handle) {
                    WinHttpSetTimeouts(handle, 5000, 5000, 15000, 30000);
                }
                return handle;
            }();
            return session;
        }

        struct ScopedHandle {
            HINTERNET handle;
            ~ScopedHandle() {
                if (handle) {
                    WinHttpCloseHandle(handle);
                }
            }
        };

        struct HttpResponse {
            DWORD status = 0;
            std::string body;
        };

        // One HTTPS request to an Epic service. Headers are CRLF-joined. nullopt on transport failure,
        // including a body cut short; an HTTP error status is still a response.
        std::optional<HttpResponse> HttpsRequest(const wchar_t *host, const wchar_t *path, const wchar_t *method, const std::wstring &headers, const std::string &body) {
            const HINTERNET session = Session();
            if (!session) {
                return std::nullopt;
            }
            const ScopedHandle connect {WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0)};
            if (!connect.handle) {
                return std::nullopt;
            }
            const ScopedHandle request {WinHttpOpenRequest(connect.handle, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
            if (!request.handle) {
                return std::nullopt;
            }
            if (!headers.empty()) {
                WinHttpAddRequestHeaders(request.handle, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            }
            const LPVOID payload = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(body.data());
            if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, payload, static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) || !WinHttpReceiveResponse(request.handle, nullptr)) {
                return std::nullopt;
            }

            HttpResponse response;
            DWORD len = sizeof(response.status);
            WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &len, WINHTTP_NO_HEADER_INDEX);

            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request.handle, &avail)) {
                    return std::nullopt;
                }
                if (avail == 0) {
                    return response; // end of the response
                }
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request.handle, chunk.data(), avail, &read)) {
                    return std::nullopt;
                }
                chunk.resize(read);
                response.body += chunk;
            }
        }

        using HttpResult = Utils::Result<std::string, AuthError>;

        // The body of a 200 response. Anything else is logged under `what`: Rejected when Epic refused
        // what was sent (HTTP 400/401), Unreachable for a transport failure or any other status.
        HttpResult HttpsOk(const char *what, const wchar_t *host, const wchar_t *path, const wchar_t *method, const std::wstring &headers, const std::string &body = {}) {
            auto response = HttpsRequest(host, path, method, headers, body);
            if (!response) {
                Logger()->warn("Epic {} request failed in transport", what);
                return HttpResult::Err(AuthError::Unreachable);
            }
            if (response->status != 200) {
                Logger()->warn("Epic {} request returned HTTP {}", what, response->status);
                const bool rejected = response->status == 400 || response->status == 401;
                return HttpResult::Err(rejected ? AuthError::Rejected : AuthError::Unreachable);
            }
            return HttpResult::Ok(std::move(response->body));
        }

        std::wstring BasicAuthHeader() {
            const std::string cred = std::string(kClientId) + ":" + kClientSecret;
            return L"Authorization: Basic " + Utils::StringUtils::Utf8ToWide(Utils::StringUtils::Base64Encode(cred)) + L"\r\nContent-Type: application/x-www-form-urlencoded";
        }

        // Tokens from an oauth/token response body; nullopt if it isn't a token grant.
        std::optional<Tokens> ParseTokens(const std::string &body) {
            try {
                const auto j = nlohmann::json::parse(body);
                Tokens tokens;
                tokens.accessToken  = j.value("access_token", std::string {});
                tokens.refreshToken = j.value("refresh_token", std::string {});
                tokens.accountId    = j.value("account_id", std::string {});
                tokens.displayName  = j.value("displayName", std::string {});
                return tokens.Valid() ? std::optional(std::move(tokens)) : std::nullopt;
            }
            catch (const std::exception &) {
                return std::nullopt;
            }
        }

        AuthResult TokenGrant(const std::string &formBody) {
            const auto body = HttpsOk("token", kAuthHost, kTokenPath, L"POST", BasicAuthHeader(), formBody);
            if (!body) {
                return AuthResult::Err(body.GetError());
            }
            auto tokens = ParseTokens(body.GetValue());
            return tokens ? AuthResult::Ok(std::move(*tokens)) : AuthResult::Err(AuthError::Unreachable);
        }

        AuthResult RefreshGrant(const std::string &refreshToken) {
            return TokenGrant("grant_type=refresh_token&token_type=eg1&refresh_token=" + FormEncode(refreshToken));
        }

        AuthResult AuthCodeGrant(const std::string &authCode) {
            return TokenGrant("grant_type=authorization_code&token_type=eg1&code=" + FormEncode(authCode));
        }

        // --- refresh-token persistence (DPAPI, tied to the Windows user) ---

        bool SaveRefreshToken(const std::string &rt) {
            if (rt.empty()) {
                return false;
            }
            DATA_BLOB in {static_cast<DWORD>(rt.size()), reinterpret_cast<BYTE *>(const_cast<char *>(rt.data()))};
            DATA_BLOB out {};
            if (!CryptProtectData(&in, L"MafiaHub-Epic", nullptr, nullptr, nullptr, 0, &out)) {
                return false;
            }
            std::ofstream f(AuthFile(), std::ios::binary | std::ios::trunc);
            const bool ok = f && f.write(reinterpret_cast<const char *>(out.pbData), out.cbData).good();
            LocalFree(out.pbData);
            return ok;
        }

        // Epic rotates the refresh token on every grant, so a failed save costs the next launch its
        // silent sign-in.
        void PersistRotatedRefreshToken(const std::string &rt) {
            if (!SaveRefreshToken(rt)) {
                Logger()->warn("Could not persist the rotated Epic refresh token; the next launch will ask to sign in again");
            }
        }

        std::optional<std::string> LoadRefreshToken() {
            std::ifstream f(AuthFile(), std::ios::binary);
            if (!f) {
                return std::nullopt;
            }
            const std::string blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (blob.empty()) {
                return std::nullopt;
            }
            DATA_BLOB in {static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE *>(const_cast<char *>(blob.data()))};
            DATA_BLOB out {};
            if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
                return std::nullopt;
            }
            std::string rt(reinterpret_cast<const char *>(out.pbData), out.cbData);
            LocalFree(out.pbData);
            return rt.empty() ? std::nullopt : std::optional(std::move(rt));
        }

        // --- interactive sign-in ---

        std::string ReadClipboardText() {
            std::string result;
            if (!OpenClipboard(nullptr)) {
                return result;
            }
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const auto *w = static_cast<const wchar_t *>(GlobalLock(h))) {
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
                    // not clean JSON (e.g. browser-rendered) — scan for the value directly
                }
                const size_t k  = s.find("authorizationCode");
                const size_t q1 = s.find('"', s.find(':', k) + 1);
                const size_t q2 = q1 == std::string::npos ? q1 : s.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    return s.substr(q1 + 1, q2 - q1 - 1);
                }
            }
            trim(s);
            return s;
        }

        std::optional<Tokens> InteractiveSignIn(const std::wstring &productName) {
            const std::wstring title = productName.empty() ? L"Epic sign-in" : productName + L" — Epic sign-in";

            ShellExecuteW(nullptr, L"open", kLoginUrl, nullptr, nullptr, SW_SHOWNORMAL);
            MessageBoxW(nullptr,
                L"A browser window was opened to sign in to Epic Games.\n\n"
                L"After you sign in, a page shows text containing an \"authorizationCode\".\n"
                L"Select and copy that code (Ctrl+C), then click OK here.",
                title.c_str(), MB_OK | MB_ICONINFORMATION);

            const std::string code = ExtractAuthCode(ReadClipboardText());
            if (code.empty()) {
                Logger()->warn("Epic sign-in: the clipboard held no authorization code");
                return std::nullopt;
            }
            auto tokens = AuthCodeGrant(code);
            if (!tokens) {
                Logger()->warn("Epic sign-in: the authorization code grant failed");
                return std::nullopt;
            }
            Logger()->info("Epic sign-in succeeded");
            return std::move(tokens).GetValue();
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

        HttpResult RequestOwnershipToken(const Tokens &tokens, const std::string &catalogNamespace, const std::string &catalogItemId) {
            if (!tokens.Valid()) {
                return HttpResult::Err(AuthError::Rejected);
            }
            if (!IsEpicId(tokens.accountId) || !IsEpicId(catalogNamespace) || !IsEpicId(catalogItemId)) {
                Logger()->warn("Epic account or catalog ids are not alphanumeric; not minting an ownership token");
                return HttpResult::Err(AuthError::Rejected);
            }
            const std::wstring path    = L"/ecommerceintegration/api/public/platforms/EPIC/identities/" + Utils::StringUtils::Utf8ToWide(tokens.accountId) + L"/ownershipToken";
            const std::wstring headers = L"Authorization: Bearer " + Utils::StringUtils::Utf8ToWide(tokens.accessToken) + L"\r\nContent-Type: application/x-www-form-urlencoded";
            const std::string body     = "nsCatalogItemId=" + FormEncode(catalogNamespace + ":" + catalogItemId);

            return HttpsOk("ownership token", kEcomHost, path.c_str(), L"POST", headers, body);
        }

        // The game consumes the complete ownership-token response as its .ovt file.
        std::wstring MintOwnershipToken(const Tokens &tokens, const std::string &catalogNamespace, const std::string &catalogItemId) {
            const auto token = RequestOwnershipToken(tokens, catalogNamespace, catalogItemId);
            if (!token) {
                return {};
            }
            if (token.GetValue().empty()) {
                Logger()->warn("Epic ownership token response was empty");
                return {};
            }

            const std::filesystem::path file = DataDir() / Utils::StringUtils::Utf8ToWide(catalogNamespace + catalogItemId + ".ovt");
            std::ofstream f(file, std::ios::binary | std::ios::trunc);
            if (!f.write(token.GetValue().data(), static_cast<std::streamsize>(token.GetValue().size()))) {
                Logger()->warn("Could not write the minted Epic ownership token");
                return {};
            }
            return file.wstring();
        }
    } // namespace

    AuthResult TryRefreshStoredAuth() {
        const auto rt = LoadRefreshToken();
        if (!rt) {
            return AuthResult::Err(AuthError::NoStoredSignIn);
        }
        auto tokens = RefreshGrant(*rt);
        if (!tokens) {
            if (tokens.GetError() == AuthError::Rejected) {
                Logger()->info("Epic refused the stored sign-in; forgetting it");
                ClearStoredAuth();
            }
            return tokens;
        }
        Logger()->info("Refreshed the stored Epic sign-in");
        PersistRotatedRefreshToken(tokens.GetValue().refreshToken);
        return tokens;
    }

    AuthResult EnsureAuthenticated(const std::wstring &productName) {
        auto stored = TryRefreshStoredAuth();
        if (stored || !NeedsSignIn(stored.GetError())) {
            return stored;
        }
        auto tokens = InteractiveSignIn(productName);
        if (!tokens) {
            return stored;
        }
        PersistRotatedRefreshToken(tokens->refreshToken);
        return AuthResult::Ok(std::move(*tokens));
    }

    std::wstring GetLoginUrl() {
        return kLoginUrl;
    }

    bool SignInWithAuthorizationCode(const std::string &pageTextOrCode) {
        const std::string code = ExtractAuthCode(pageTextOrCode);
        if (code.empty()) {
            Logger()->warn("Epic sign-in: the page held no authorization code");
            return false;
        }
        const auto tokens = AuthCodeGrant(code);
        if (!tokens) {
            Logger()->warn("Epic sign-in: the authorization code grant failed");
            return false;
        }
        // The tokens are dropped here, so the stored refresh token is the only result there is.
        if (!SaveRefreshToken(tokens.GetValue().refreshToken)) {
            Logger()->error("Epic sign-in succeeded, but the refresh token could not be persisted");
            return false;
        }
        Logger()->info("Epic sign-in succeeded");
        return true;
    }

    std::optional<std::string> GetExchangeCode(const Tokens &tokens) {
        if (!tokens.Valid()) {
            return std::nullopt;
        }
        const auto body = HttpsOk("exchange code", kAuthHost, kExchPath, L"GET", L"Authorization: Bearer " + Utils::StringUtils::Utf8ToWide(tokens.accessToken));
        if (!body) {
            return std::nullopt;
        }
        try {
            std::string code = nlohmann::json::parse(body.GetValue()).value("code", std::string {});
            return code.empty() ? std::nullopt : std::optional(std::move(code));
        }
        catch (const std::exception &) {
            return std::nullopt;
        }
    }

    std::optional<std::string> GetAccountProof(const std::string &catalogNamespace, const std::string &catalogItemId) {
        const auto auth = TryRefreshStoredAuth();
        if (!auth)
            return std::nullopt;
        const auto response = RequestOwnershipToken(auth.GetValue(), catalogNamespace, catalogItemId);
        if (!response)
            return std::nullopt;
        try {
            const auto proof = nlohmann::json::parse(response.GetValue()).value("token", std::string {});
            return AccountProofKeyId(proof) ? std::optional(proof) : std::nullopt;
        }
        catch (const nlohmann::json::exception &) {
            return std::nullopt;
        }
    }

    std::wstring BuildLaunchArgs(const Tokens &tokens, const std::string &exchangeCode, const std::string &appName, const std::string &sandboxId, const std::string &catalogItemId, const std::string &installDir) {
        using Utils::StringUtils::Utf8ToWide;
        std::wstring a = L" -AUTH_LOGIN=unused -AUTH_PASSWORD=" + Utf8ToWide(exchangeCode) + L" -AUTH_TYPE=exchangecode";
        if (!appName.empty()) {
            a += L" -epicapp=" + Utf8ToWide(appName);
        }
        a += L" -epicenv=Prod";

        // Ownership proof — the piece the "use the Epic launcher" gate actually checks.
        if (!sandboxId.empty()) {
            a += L" -epicsandboxid=" + Utf8ToWide(sandboxId);
        }
        std::wstring ovt = MintOwnershipToken(tokens, sandboxId, catalogItemId);
        if (!ovt.empty()) {
            Logger()->info("Minted a fresh Epic ownership token");
        }
        else if (ovt = FindOwnershipToken(installDir); !ovt.empty()) {
            Logger()->info("Falling back to the Epic launcher's .egstore ownership token");
        }
        else {
            Logger()->warn("No Epic ownership token was minted or found under .egstore");
        }
        if (!ovt.empty()) {
            a += L" -epicovt=\"" + ovt + L"\"";
        }

        if (!tokens.accountId.empty()) {
            a += L" -epicuserid=" + Utf8ToWide(tokens.accountId);
        }
        if (const std::string name = QuotableArgument(tokens.displayName); !name.empty()) {
            a += L" -epicusername=\"" + Utf8ToWide(name) + L"\"";
        }
        a += L" -epiclocale=en -EpicPortal";
        return a;
    }

    void ClearStoredAuth() {
        std::error_code ec;
        std::filesystem::remove(AuthFile(), ec);
    }
} // namespace Framework::External::Epic
