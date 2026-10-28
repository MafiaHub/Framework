/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Framework::Integrations::Server {
    // Declares GET /files/<name> on the framework's webserver and serves the client resource packages
    // and asset paks there, so downloads stay off the game connection. Only published names are
    // served: the staging directory beside them holds the pak builder's indexes too. A request must carry a
    // token the server handed to a connected peer (kTokenHeader), and at most maxTransfers files are
    // sent at once so the webserver's other routes keep workers; the rest are told to retry.
    class AssetFileServer final {
      public:
        static constexpr const char *kTokenHeader = "X-Download-Token";

        bool Register(size_t maxTransfers);

        void Publish(std::unordered_map<std::string, std::filesystem::path> files);

        // A fresh token for this peer, replacing any it held; empty if no randomness was available.
        std::string Grant(uint64_t peer);
        void Revoke(uint64_t peer);

      private:
        bool IsGranted(const std::string &token);

        std::mutex _filesMutex;
        std::unordered_map<std::string, std::filesystem::path> _files;

        std::mutex _tokensMutex;
        std::unordered_map<uint64_t, std::string> _tokens;

        size_t _maxTransfers = 1;
        std::atomic<size_t> _transfers {};
    };
} // namespace Framework::Integrations::Server
