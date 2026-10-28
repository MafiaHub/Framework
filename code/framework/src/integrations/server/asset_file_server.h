/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <http/webserver.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Framework::Integrations::Server {
    // Serves the client resource packages and asset paks as GET /files/<name>, so downloads stay
    // off the game connection. Only published names are served: the staging directory beside them
    // also holds the package key.
    class AssetFileServer final {
      public:
        void Register(const HTTP::Webserver &webServer);

        void Publish(std::unordered_map<std::string, std::filesystem::path> files);

      private:
        std::mutex _filesMutex;
        std::unordered_map<std::string, std::filesystem::path> _files;
    };
} // namespace Framework::Integrations::Server
