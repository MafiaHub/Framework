/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "masterlist_browser.h"

#include <logging/logger.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <utility>

namespace Framework::Integrations::Client {
    namespace {
        constexpr const char *kListPath = "/servers";
        constexpr int kTimeoutSeconds   = 8;
        constexpr uint16_t kDefaultPort = 27015;

        auto Log() {
            return Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
        }
    } // namespace

    MasterlistBrowser::~MasterlistBrowser() {
        Shutdown();
    }

    bool MasterlistBrowser::Init(const std::string &masterlistUrl, const std::string &gameMode) {
        if (masterlistUrl.empty()) {
            Log()->error("Masterlist URL is not set");
            return false;
        }
        _masterlistUrl = masterlistUrl;
        _gameMode      = gameMode;
        _status.store(Status::Idle, std::memory_order_release);
        return true;
    }

    void MasterlistBrowser::Refresh() {
        if (_masterlistUrl.empty()) {
            return;
        }
        // exchange, not a load/store pair: Refresh is callable from any thread and two callers must
        // not both start a fetch.
        if (_running.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (_worker.joinable()) {
            _worker.join(); // the previous fetch has finished; reap it before reusing the slot
        }
        _status.store(Status::Fetching, std::memory_order_release);
        _worker = std::thread(&MasterlistBrowser::Fetch, this);
    }

    void MasterlistBrowser::Fetch() {
        std::vector<MasterlistServer> parsed;
        std::string error;

        try {
            httplib::Client client(_masterlistUrl);
            client.set_connection_timeout(kTimeoutSeconds, 0);
            client.set_read_timeout(kTimeoutSeconds, 0);
            client.set_follow_location(true);

            auto res = client.Get(kListPath);
            if (!res) {
                error = "masterlist unreachable";
            }
            else if (res->status != 200) {
                error = "masterlist returned HTTP " + std::to_string(res->status);
            }
            else {
                const auto doc = nlohmann::json::parse(res->body, nullptr, false);
                if (doc.is_discarded() || !doc.is_array()) {
                    error = "masterlist returned malformed JSON";
                }
                else {
                    for (const auto &item : doc) {
                        if (!item.is_object()) {
                            continue;
                        }
                        MasterlistServer entry;
                        entry.gameMode = item.value("gamemode", std::string {});
                        // One masterlist serves every MafiaHub game.
                        if (!_gameMode.empty() && entry.gameMode != _gameMode) {
                            continue;
                        }
                        entry.name           = item.value("name", std::string {});
                        entry.host           = item.value("ip", std::string {});
                        entry.port           = static_cast<uint16_t>(item.value("port", static_cast<int>(kDefaultPort)));
                        entry.localization   = item.value("localization", std::string {});
                        entry.version        = item.value("version", std::string {});
                        entry.currentPlayers = item.value("current_players", 0);
                        entry.maxPlayers     = item.value("max_players", 0);
                        if (entry.host.empty()) {
                            continue; // nothing to dial
                        }
                        if (entry.name.empty()) {
                            entry.name = entry.Address();
                        }
                        parsed.push_back(std::move(entry));
                    }
                }
            }
        }
        catch (const std::exception &ex) {
            error = std::string("masterlist fetch threw: ") + ex.what();
        }
        catch (...) {
            error = "masterlist fetch threw";
        }

        {
            std::lock_guard lock(_mutex);
            _results   = std::move(parsed);
            _lastError = error;
        }
        _status.store(error.empty() ? Status::Ready : Status::Failed, std::memory_order_release);
        // Publish before clearing _running, so a Poll that sees the result cannot race a Refresh
        // that has already started replacing it.
        _pending.store(true, std::memory_order_release);
        _running.store(false, std::memory_order_release);

        if (!error.empty()) {
            Log()->warn("Masterlist fetch failed: {}", error);
        }
    }

    bool MasterlistBrowser::Poll(std::vector<MasterlistServer> &out) {
        if (!_pending.exchange(false, std::memory_order_acq_rel)) {
            return false;
        }
        std::lock_guard lock(_mutex);
        out = _results;
        return true;
    }

    MasterlistBrowser::Status MasterlistBrowser::State() const {
        return _status.load(std::memory_order_acquire);
    }

    std::string MasterlistBrowser::LastError() const {
        std::lock_guard lock(_mutex);
        return _lastError;
    }

    void MasterlistBrowser::Shutdown() {
        if (_worker.joinable()) {
            _worker.join();
        }
        _running.store(false, std::memory_order_release);
        _pending.store(false, std::memory_order_release);
    }
} // namespace Framework::Integrations::Client
