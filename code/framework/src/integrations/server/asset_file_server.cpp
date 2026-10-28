/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "asset_file_server.h"

#include <core_modules.h>
#include <http/webserver.h>
#include <logging/logger.h>
#include <utils/crypto.h>

#include <httplib.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>

namespace Framework::Integrations::Server {
    namespace {
        // What a client refused for load waits before asking again.
        constexpr const char *kRetryAfterSeconds = "2";
    } // namespace

    bool AssetFileServer::Register(size_t maxTransfers) {
        const auto webServer = CoreModules::GetWebserver();
        if (!webServer) {
            return false;
        }
        _maxTransfers = std::max<size_t>(maxTransfers, 1);
        webServer->RegisterRequest(R"(/files/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
            if (!IsGranted(req.get_header_value(kTokenHeader))) {
                res.status = 403;
                return;
            }

            std::filesystem::path path;
            {
                std::lock_guard lock(_filesMutex);
                const auto file = _files.find(req.matches[1].str());
                if (file == _files.end()) {
                    res.status = 404;
                    return;
                }
                path = file->second;
            }

            auto stream = std::make_shared<std::ifstream>(path, std::ios::binary);
            std::error_code code;
            const auto size = std::filesystem::file_size(path, code);
            if (!stream->is_open() || code) {
                res.status = 404;
                return;
            }

            // A transfer holds its worker until the last byte, so the slot is taken here and given back
            // by the releaser, which httplib runs however the response ends.
            if (_transfers.fetch_add(1) >= _maxTransfers) {
                _transfers.fetch_sub(1);
                res.status = 503;
                res.set_header("Retry-After", kRetryAfterSeconds);
                return;
            }
            res.set_content_provider(
                static_cast<size_t>(size), "application/octet-stream",
                [stream](size_t offset, size_t length, httplib::DataSink &sink) {
                    char buffer[65536];
                    stream->seekg(static_cast<std::streamoff>(offset));
                    const auto read = stream->read(buffer, static_cast<std::streamsize>(std::min(length, sizeof(buffer)))).gcount();
                    return read > 0 && sink.write(buffer, static_cast<size_t>(read));
                },
                [this](bool) {
                    _transfers.fetch_sub(1);
                });
        });
        return true;
    }

    std::string AssetFileServer::Grant(uint64_t peer) {
        std::array<uint8_t, 32> bytes {};
        if (!Utils::Crypto::RandomBytes(bytes.data(), bytes.size())) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("No randomness for a download token; peer {} cannot fetch files", peer);
            Revoke(peer);
            return {};
        }
        std::string token = Utils::Crypto::ToHex(bytes.data(), bytes.size());
        std::lock_guard lock(_tokensMutex);
        _tokens[peer] = token;
        return token;
    }

    void AssetFileServer::Revoke(uint64_t peer) {
        std::lock_guard lock(_tokensMutex);
        _tokens.erase(peer);
    }

    bool AssetFileServer::IsGranted(const std::string &token) {
        if (token.empty()) {
            return false;
        }
        std::lock_guard lock(_tokensMutex);
        return std::any_of(_tokens.begin(), _tokens.end(), [&token](const auto &entry) {
            return Utils::Crypto::ConstantTimeEquals(entry.second, token);
        });
    }

    void AssetFileServer::Publish(std::unordered_map<std::string, std::filesystem::path> files) {
        std::lock_guard lock(_filesMutex);
        _files = std::move(files);
    }
} // namespace Framework::Integrations::Server
