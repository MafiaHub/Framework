/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "asset_file_server.h"

#include <httplib.h>

#include <algorithm>
#include <fstream>
#include <memory>

namespace Framework::Integrations::Server {
    void AssetFileServer::Register(const HTTP::Webserver &webServer) {
        webServer.RegisterRequest(R"(/files/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
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
            res.set_content_provider(static_cast<size_t>(size), "application/octet-stream", [stream](size_t offset, size_t length, httplib::DataSink &sink) {
                char buffer[65536];
                stream->seekg(static_cast<std::streamoff>(offset));
                const auto read = stream->read(buffer, static_cast<std::streamsize>(std::min(length, sizeof(buffer)))).gcount();
                return read > 0 && sink.write(buffer, static_cast<size_t>(read));
            });
        });
    }

    void AssetFileServer::Publish(std::unordered_map<std::string, std::filesystem::path> files) {
        std::lock_guard lock(_filesMutex);
        _files = std::move(files);
    }
} // namespace Framework::Integrations::Server
