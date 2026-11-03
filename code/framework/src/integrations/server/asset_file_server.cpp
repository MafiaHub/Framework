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
#include <utils/streamed_assets/transfer.h>

#include <httplib.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <vector>

namespace Framework::Integrations::Server {
    namespace {
        // Read per provider call: large enough that a transfer is bound by the socket, not the disk.
        constexpr size_t kReadChunk = 1024 * 1024;
        // A refused peer keeps its place this long after it last asked; a peer that left stops asking.
        constexpr auto kQueueEntryLifetime = std::chrono::seconds(AssetFileServer::kRetryAfterSeconds * 4);
    } // namespace

    bool AssetFileServer::Register(size_t maxTransfers) {
        const auto webServer = CoreModules::GetWebserver();
        if (!webServer) {
            return false;
        }
        _maxTransfers = std::max<size_t>(maxTransfers, 1);
        _maxClients   = _maxTransfers;
        webServer->RegisterRequest(R"(/files/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
            const std::string token = req.get_header_value(kTokenHeader);
            if (!IsGranted(token)) {
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

            // A chunk list is a few hundred bytes a queued peer needs before its turn; it takes no slot.
            if (path.extension() == ".chunks") {
                std::ifstream list(path, std::ios::binary);
                res.set_content(std::string(std::istreambuf_iterator<char>(list), std::istreambuf_iterator<char>()), "text/plain");
                return;
            }

            // A transfer holds its worker until the last byte, so the slot is taken here and given back
            // by the releaser, which httplib runs however the response ends.
            const Admission admission = Acquire(token);
            if (!admission.granted) {
                res.status = 503;
                res.set_header("Retry-After", std::to_string(kRetryAfterSeconds));
                res.set_header(kQueueHeader, std::to_string(admission.queuePosition));
                return;
            }
            auto buffer = std::make_shared<std::vector<char>>(kReadChunk);
            res.set_content_provider(
                static_cast<size_t>(size), "application/octet-stream",
                [stream, buffer](size_t offset, size_t length, httplib::DataSink &sink) {
                    stream->seekg(static_cast<std::streamoff>(offset));
                    const auto read = stream->read(buffer->data(), static_cast<std::streamsize>(std::min(length, buffer->size()))).gcount();
                    return read > 0 && sink.write(buffer->data(), static_cast<size_t>(read));
                },
                [this, token](bool) {
                    Release(token);
                });
        });
        // Declared after the file route, which wins for a published name: every other path under /files/
        // ends here, so the static directory never answers for it.
        webServer->RegisterRequest(R"(/files(/.*)?)", [](const httplib::Request &, httplib::Response &res) {
            res.status = 404;
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
        std::string token;
        {
            std::lock_guard lock(_tokensMutex);
            const auto it = _tokens.find(peer);
            if (it == _tokens.end()) {
                return;
            }
            token = it->second;
            _tokens.erase(it);
        }
        // Its transfers end on their own and release themselves; only its place in line goes now.
        std::lock_guard lock(_slotsMutex);
        _queue.erase(std::remove_if(_queue.begin(), _queue.end(),
                         [&token](const auto &entry) {
                             return entry.first == token;
                         }),
            _queue.end());
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

    void AssetFileServer::ExpireQueue(std::chrono::steady_clock::time_point now) {
        _queue.erase(std::remove_if(_queue.begin(), _queue.end(),
                         [now](const auto &entry) {
                             return now - entry.second > kQueueEntryLifetime;
                         }),
            _queue.end());
        // A peer between two files of its set keeps its slot for a moment, so a burst does not push it
        // to the back of the line after every file; one that is done lets go.
        for (auto it = _slots.begin(); it != _slots.end();) {
            if (it->second.open == 0 && now - it->second.idleSince > kQueueEntryLifetime) {
                it = _slots.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    AssetFileServer::Admission AssetFileServer::Acquire(const std::string &token) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(_slotsMutex);
        ExpireQueue(now);

        // Every open transfer holds a worker, so the budget binds slot holders and the line alike.
        size_t openTransfers = 0;
        size_t open          = 0;
        for (const auto &entry : _slots) {
            openTransfers += entry.second.open;
            open += entry.second.open > 0 ? 1 : 0;
        }
        const size_t freeTransfers = _maxTransfers > openTransfers ? _maxTransfers - openTransfers : 0;

        // A peer holding a slot opens more connections on it, up to its share and the budget.
        if (const auto held = _slots.find(token); held != _slots.end()) {
            if (held->second.open >= kMaxTransfersPerClient || freeTransfers == 0) {
                return {false, 0};
            }
            ++held->second.open;
            return {true, 0};
        }

        // A slot kept idle for its holder's next file is spare capacity for the line, not a hold on it.
        const size_t free = std::min(_maxClients > open ? _maxClients - open : 0, freeTransfers);
        auto place        = std::find_if(_queue.begin(), _queue.end(), [&token](const auto &entry) {
            return entry.first == token;
        });
        if (place == _queue.end()) {
            _queue.emplace_back(token, now);
            place = std::prev(_queue.end());
        }
        place->second         = now;
        const size_t position = static_cast<size_t>(std::distance(_queue.begin(), place));
        // Slots go to the front of the line: the free ones cover the first |free| waiting peers.
        if (position >= free) {
            return {false, position};
        }
        _queue.erase(place);
        if (_slots.size() >= _maxClients) {
            // Every slot is held and at least one sits idle: the longest idle one goes to the newcomer.
            const auto idle = std::min_element(_slots.begin(), _slots.end(), [](const auto &a, const auto &b) {
                if ((a.second.open == 0) != (b.second.open == 0)) {
                    return a.second.open == 0;
                }
                return a.second.idleSince < b.second.idleSince;
            });
            _slots.erase(idle);
        }
        _slots[token] = Slot {1, now};
        return {true, 0};
    }

    void AssetFileServer::Release(const std::string &token) {
        std::lock_guard lock(_slotsMutex);
        const auto held = _slots.find(token);
        if (held == _slots.end() || held->second.open == 0) {
            return;
        }
        if (--held->second.open == 0) {
            held->second.idleSince = std::chrono::steady_clock::now();
        }
    }

    size_t AssetFileServer::GetActiveClients() {
        std::lock_guard lock(_slotsMutex);
        return static_cast<size_t>(std::count_if(_slots.begin(), _slots.end(), [](const auto &entry) {
            return entry.second.open > 0;
        }));
    }

    size_t AssetFileServer::GetQueuedClients() {
        std::lock_guard lock(_slotsMutex);
        ExpireQueue(std::chrono::steady_clock::now());
        return _queue.size();
    }

    void AssetFileServer::Publish(std::unordered_map<std::string, std::filesystem::path> files) {
        std::unordered_map<std::string, std::filesystem::path> lists;
        for (const auto &[name, path] : files) {
            const std::filesystem::path list = path.string() + ".chunks";
            std::error_code code;
            const bool stale = !std::filesystem::exists(list, code) || std::filesystem::last_write_time(list, code) < std::filesystem::last_write_time(path, code) || code;
            if (stale) {
                const std::vector<std::string> chunks = Utils::StreamedAssets::HashFileChunks(path.string());
                if (chunks.empty()) {
                    continue;
                }
                std::ofstream out(list, std::ios::binary | std::ios::trunc);
                out << Utils::StreamedAssets::FormatChunkList(chunks);
                if (!out) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Could not write the chunk list of '{}'; clients read it whole", name);
                    continue;
                }
            }
            lists[Utils::StreamedAssets::ChunkListFileName(name)] = list;
        }
        files.insert(lists.begin(), lists.end());
        std::lock_guard lock(_filesMutex);
        _files = std::move(files);
    }
} // namespace Framework::Integrations::Server
