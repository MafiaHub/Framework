/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#include "account_key.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <utils/safe_win32.h>

#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Framework::Networking {
    namespace {
        // One line: the format tag, a space, the seed in hex. Text, so a player can tell what it is.
        constexpr std::string_view kKeyFileTag = "mafiahub-account-key-v1";

        template <std::size_t N>
        bool CopyExact(const std::string &from, std::array<uint8_t, N> &to) {
            if (from.size() != N) {
                return false;
            }
            std::memcpy(to.data(), from.data(), N);
            return true;
        }

        bool ParseKeyFile(const std::string &contents, Utils::Crypto::SigningSeed &outSeed) {
            std::istringstream in(contents);
            std::string tag;
            std::string hex;
            if (!(in >> tag >> hex) || tag != kKeyFileTag) {
                return false;
            }
            std::string rest;
            if (in >> rest) {
                return false;
            }
            const bool ok = Utils::Crypto::FromHex(hex, outSeed.data(), outSeed.size());
            Utils::Crypto::SecureZero(hex.data(), hex.size());
            return ok;
        }

        enum class WriteResult {
            Written,
            Failed,
            Exists
        };

        // A new file only its owner can open, created with that protection rather than given it after
        // the seed is in: a protected DACL on Windows, mode 0600 on POSIX, whatever the umask.
        bool WriteOwnerOnly(const std::filesystem::path &staging, const std::string &contents) {
#ifdef _WIN32
            // Protected (no inherited entries), full access for this user and SYSTEM only. The user by
            // SID rather than as the file's owner: an elevated run makes Administrators the owner, and
            // the same user's next, unelevated run could not read its own account.
            std::wstring user;
            {
                HANDLE token = nullptr;
                if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                    return false;
                }
                DWORD size = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &size);
                std::vector<uint8_t> buffer(size);
                LPWSTR sid    = nullptr;
                const bool ok = size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size) && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid);
                CloseHandle(token);
                if (!ok) {
                    return false;
                }
                user = sid;
                LocalFree(sid);
            }
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const std::wstring sddl         = L"D:P(A;;FA;;;" + user + L")(A;;FA;;;SY)";
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
                return false;
            }
            SECURITY_ATTRIBUTES attributes {sizeof(attributes), descriptor, FALSE};
            const HANDLE file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            LocalFree(descriptor);
            if (file == INVALID_HANDLE_VALUE) {
                return false;
            }
            DWORD written = 0;
            const bool ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size() && FlushFileBuffers(file);
            CloseHandle(file);
            return ok;
#else
            const int file = open(staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (file < 0) {
                return false;
            }
            size_t done = 0;
            while (done < contents.size()) {
                const ssize_t written = write(file, contents.data() + done, contents.size() - done);
                if (written < 0 && errno == EINTR) {
                    continue;
                }
                if (written <= 0) {
                    break;
                }
                done += static_cast<size_t>(written);
            }
            const bool ok = done == contents.size() && fsync(file) == 0;
            return close(file) == 0 && ok;
#endif
        }

        // Moves the staged key into place only if there is no key there yet, in one step: the first key
        // to land is the account, whoever wrote it. Exists when another process got there first.
        WriteResult Publish(const std::filesystem::path &staging, const std::filesystem::path &path) {
#ifdef _WIN32
            // Without MOVEFILE_REPLACE_EXISTING the move fails on an existing target; the DACL moves with it.
            if (MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
                return WriteResult::Written;
            }
            const DWORD error = GetLastError();
            return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? WriteResult::Exists : WriteResult::Failed;
#else
            // link() never replaces; there is no fallback to rename(), which would.
            if (link(staging.c_str(), path.c_str()) == 0) {
                return WriteResult::Written;
            }
            return errno == EEXIST ? WriteResult::Exists : WriteResult::Failed;
#endif
        }

        // Written beside the target and moved into place, so an interrupted write never leaves half a
        // key. Exists when another process created the key meanwhile: theirs is kept, ours dropped.
        WriteResult WriteKeyFile(const std::filesystem::path &path, const Utils::Crypto::SigningSeed &seed, std::string &outError) {
            std::error_code code;
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path(), code);
                if (code) {
                    outError = fmt::format("could not create {}: {}", AccountKeyPathText(path.parent_path()), code.message());
                    return WriteResult::Failed;
                }
            }

            uint8_t suffix[8] {};
            if (!Utils::Crypto::RandomBytes(suffix, sizeof(suffix))) {
                outError = "no secure random source to stage the account key";
                return WriteResult::Failed;
            }
            std::filesystem::path staging = path;
            staging += "." + Utils::Crypto::ToHex(suffix, sizeof(suffix)) + ".new";
            std::string line   = fmt::format("{} {}\n", kKeyFileTag, Utils::Crypto::ToHex(seed.data(), seed.size()));
            const bool written = WriteOwnerOnly(staging, line);
            Utils::Crypto::SecureZero(line.data(), line.size());
            std::error_code ignored;
            if (!written) {
                std::filesystem::remove(staging, ignored);
                outError = fmt::format("could not write {}", AccountKeyPathText(staging));
                return WriteResult::Failed;
            }

            const WriteResult published = Publish(staging, path);
            // Gone after a move, still there after a link or a failure.
            std::filesystem::remove(staging, ignored);
            if (published == WriteResult::Failed) {
                outError = fmt::format("could not move the new key to {}", AccountKeyPathText(path));
            }
            return published;
        }
    } // namespace

    std::string AccountKeyPathText(const std::filesystem::path &path) {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    std::string AccountIdOf(std::string_view publicKey) {
        if (publicKey.size() != RPC::ClientIdentity::kPublicKeyLength) {
            return {};
        }
        return Utils::Crypto::Sha256Hex(publicKey.data(), publicKey.size()).substr(0, 32);
    }

    std::optional<AccountKey> AccountKey::FromSeed(const Utils::Crypto::SigningSeed &seed) {
        AccountKey key;
        key._seed = seed;
        if (!Utils::Crypto::DerivePublicKey(key._seed, key._publicKey)) {
            return std::nullopt;
        }
        key._accountId = AccountIdOf(std::string_view(reinterpret_cast<const char *>(key._publicKey.data()), key._publicKey.size()));
        return key;
    }

    std::optional<AccountKey> AccountKey::LoadOrCreate(const std::filesystem::path &path, std::string &outError) {
        std::error_code code;
        const bool exists = std::filesystem::exists(path, code);
        if (code) {
            outError = fmt::format("could not check {}: {}", AccountKeyPathText(path), code.message());
            return std::nullopt;
        }

        Utils::Crypto::SigningSeed seed {};
        const auto read = [&] {
            std::ifstream in(path, std::ios::binary);
            std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const bool parsed = !in.bad() && ParseKeyFile(contents, seed);
            Utils::Crypto::SecureZero(contents.data(), contents.size());
            if (!parsed) {
                outError = fmt::format("{} is not a valid account key", AccountKeyPathText(path));
            }
            return parsed;
        };

        if (exists) {
            if (!read()) {
                return std::nullopt;
            }
        }
        else {
            if (!Utils::Crypto::RandomBytes(seed.data(), seed.size())) {
                outError = "no secure random source to create an account key";
                return std::nullopt;
            }
            const WriteResult written = WriteKeyFile(path, seed, outError);
            if (written == WriteResult::Failed || (written == WriteResult::Exists && !read())) {
                Utils::Crypto::SecureZero(seed.data(), seed.size());
                return std::nullopt;
            }
        }

        auto key = FromSeed(seed);
        Utils::Crypto::SecureZero(seed.data(), seed.size());
        if (!key) {
            outError = fmt::format("{} holds a key OpenSSL refused", AccountKeyPathText(path));
        }
        return key;
    }

    AccountKey::~AccountKey() {
        Utils::Crypto::SecureZero(_seed.data(), _seed.size());
    }

    bool AccountKey::Sign(RPC::ClientIdentity &identity, uint64_t nowUnix) const {
        identity.publicKey.assign(reinterpret_cast<const char *>(_publicKey.data()), _publicKey.size());
        identity.issuedAt = nowUnix;
        identity.nonce.resize(RPC::ClientIdentity::kNonceLength);
        identity.signature.clear();
        if (!Utils::Crypto::RandomBytes(reinterpret_cast<uint8_t *>(identity.nonce.data()), identity.nonce.size())) {
            return false;
        }

        const std::string payload = identity.SigningPayload();
        Utils::Crypto::Signature signature {};
        if (!Utils::Crypto::Sign(_seed, payload.data(), payload.size(), signature)) {
            return false;
        }
        identity.signature.assign(reinterpret_cast<const char *>(signature.data()), signature.size());
        return true;
    }

    AccountProofResult AccountProofVerifier::Verify(const RPC::ClientIdentity &identity, uint64_t nowUnix) {
        if (identity.publicKey.empty() || identity.signature.empty()) {
            return AccountProofResult::Missing;
        }

        Utils::Crypto::PublicKey publicKey {};
        Utils::Crypto::Signature signature {};
        if (!CopyExact(identity.publicKey, publicKey) || !CopyExact(identity.signature, signature) || identity.nonce.size() != RPC::ClientIdentity::kNonceLength) {
            return AccountProofResult::Malformed;
        }

        // The signature first: an unsigned request says nothing true about its clock either.
        const std::string payload = identity.SigningPayload();
        if (!Utils::Crypto::Verify(publicKey, payload.data(), payload.size(), signature)) {
            return AccountProofResult::BadSignature;
        }

        if (identity.issuedAt > nowUnix + kMaxClockSkewSeconds) {
            return AccountProofResult::ClockAhead;
        }
        if (identity.issuedAt + kMaxClockSkewSeconds < nowUnix) {
            return AccountProofResult::ClockBehind;
        }

        // Keyed on what the signature covers rather than the signature itself, so an equivalent
        // encoding of the same signature cannot pass as a new request.
        std::string key = identity.publicKey + identity.nonce;
        const std::lock_guard guard(_mutex);
        Forget(nowUnix);
        if (_seen.contains(key)) {
            return AccountProofResult::Replayed;
        }
        if (_seen.size() >= _capacity) {
            return AccountProofResult::Busy;
        }
        _seen.insert(key);
        _byExpiry.emplace(identity.issuedAt + kMaxClockSkewSeconds, std::move(key));
        return AccountProofResult::Valid;
    }

    void AccountProofVerifier::Forget(uint64_t nowUnix) {
        while (!_byExpiry.empty() && _byExpiry.begin()->first < nowUnix) {
            _seen.erase(_byExpiry.begin()->second);
            _byExpiry.erase(_byExpiry.begin());
        }
    }

    std::string AccountProofVerifier::RefusalFor(AccountProofResult result, const RPC::ClientIdentity &identity, uint64_t nowUnix) {
        switch (result) {
        case AccountProofResult::Valid: return {};
        case AccountProofResult::ClockAhead:
        case AccountProofResult::ClockBehind: {
            const uint64_t skew    = identity.issuedAt > nowUnix ? identity.issuedAt - nowUnix : nowUnix - identity.issuedAt;
            const uint64_t minutes = std::max<uint64_t>(1, (skew + 30) / 60);
            return fmt::format("Your computer's clock is {} minute{} {}. Set it to the correct time and try again.", minutes, minutes == 1 ? "" : "s", result == AccountProofResult::ClockAhead ? "fast" : "slow");
        }
        case AccountProofResult::Replayed: return "This connection request was already used. Try again.";
        case AccountProofResult::Busy: return "The server is busy checking connections. Try again in a minute.";
        case AccountProofResult::Missing:
        case AccountProofResult::Malformed:
        case AccountProofResult::BadSignature: break;
        }
        return "Your client could not prove its account. Update it and try again.";
    }
} // namespace Framework::Networking
