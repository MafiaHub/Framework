/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "session.h"

#include <httplib.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace Framework::Automation {
    namespace {
        std::int64_t Now() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        std::string Environment(const char *name) {
            const char *value = std::getenv(name);
            return value != nullptr ? value : "";
        }
        void Reply(httplib::Response &response, const nlohmann::json &value, int status = 200) {
            response.status = status;
            response.set_content(value.dump(), "application/json");
        }
    } // namespace

    bool IsInside(const std::filesystem::path &path, const std::filesystem::path &root) {
        const auto target = std::filesystem::weakly_canonical(path);
        const auto base   = std::filesystem::weakly_canonical(root);
        auto child        = target.begin();
        for (auto parent = base.begin(); parent != base.end(); ++parent, ++child) {
            if (child == target.end())
                return false;
#ifdef _WIN32
            if (_wcsicmp(child->c_str(), parent->c_str()) != 0)
                return false;
#else
            if (*child != *parent)
                return false;
#endif
        }
        return true;
    }

    Options Options::FromEnvironment() {
        Options result;
        const auto directory = Environment("FW_QA_DIR");
        if (directory.empty())
            return result;
#ifdef NDEBUG
        throw std::runtime_error("QA automation requires a Debug build");
#else
        result.directory = directory;
        result.workspace = Environment("FW_QA_WORKSPACE");
        result.token     = Environment("FW_QA_TOKEN");
        result.role      = Environment("FW_QA_ROLE");
        if (!result.directory.is_absolute() || !result.workspace.is_absolute() || !std::filesystem::is_directory(result.directory) || !std::filesystem::is_directory(result.workspace) || !IsInside(result.directory, result.workspace)
            || std::filesystem::equivalent(result.directory, result.workspace))
            throw std::runtime_error("FW_QA_DIR must be an existing directory strictly inside FW_QA_WORKSPACE");
        if (result.token.size() < 32 || result.token.size() > 128 || result.role.empty() || result.role.size() > 64)
            throw std::runtime_error("QA requires a 32-128 character token and a role");
        result.directory = std::filesystem::canonical(result.directory);
        result.workspace = std::filesystem::canonical(result.workspace);
        result.enabled   = true;
        return result;
#endif
    }

    Session::Session() = default;
    Session::~Session() {
        Stop();
    }

    void Session::Start(const Options &options, const nlohmann::json &capabilities) {
        if (!options.enabled)
            return;
        if (_server || std::filesystem::exists(options.directory / "endpoint.json"))
            throw std::runtime_error("QA session requires a fresh instance directory");
        _options = options;
        _lastContact.store(Now());
        _server                 = std::make_unique<httplib::Server>();
        _server->new_task_queue = [] {
            return new httplib::ThreadPool(2);
        };
        _server->set_payload_max_length(65536);
        _server->set_read_timeout(5, 0);
        _server->set_write_timeout(5, 0);
        _server->set_pre_routing_handler([this](const httplib::Request &request, httplib::Response &response) {
            if (request.get_header_value("Authorization") != "Bearer " + _options.token) {
                Reply(response, {{"error", "Unauthorized"}}, 401);
                return httplib::Server::HandlerResponse::Handled;
            }
            _lastContact.store(Now(), std::memory_order_relaxed);
            return httplib::Server::HandlerResponse::Unhandled;
        });
        _server->Get("/status", [this, capabilities](const auto &, auto &response) {
            std::lock_guard lock(_mutex);
            Reply(response, {{"role", _options.role}, {"tick", Tick()}, {"capabilities", capabilities}, {"snapshot", _snapshot}, {"last_sequence", _sequence}, {"evidence_lost", _journalOverflow || _journalFailed.load()}});
        });
        _server->Get("/events", [this](const auto &request, auto &response) {
            try {
                std::uint64_t after = 0;
                if (request.has_param("after")) {
                    const auto value = request.get_param_value("after");
                    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                        throw std::runtime_error("Invalid cursor");
                    after = std::stoull(value);
                }
                std::lock_guard lock(_mutex);
                auto events = nlohmann::json::array();
                for (const auto &event : _events)
                    if (event.at("sequence").template get<std::uint64_t>() > after)
                        events.push_back(event);
                Reply(response, {{"events", events}, {"last_sequence", _sequence}, {"gap", !_events.empty() && after < _events.front().at("sequence").template get<std::uint64_t>() - 1}, {"evidence_lost", _journalOverflow || _journalFailed.load()}});
            }
            catch (const std::exception &error) {
                Reply(response, {{"error", error.what()}}, 400);
            }
        });
        _server->Post("/command", [this](const auto &request, auto &response) {
            try {
                const auto body      = nlohmann::json::parse(request.body);
                const auto id        = body.at("request_id").template get<std::string>();
                const auto operation = body.at("operation").template get<std::string>();
                const auto arguments = body.value("arguments", nlohmann::json::object());
                if (id.empty() || id.size() > 128 || operation.empty() || operation.size() > 64 || !arguments.is_object())
                    throw std::runtime_error("Invalid command");
                const auto fingerprint = nlohmann::json({{"operation", operation}, {"arguments", arguments}}).dump();
                std::lock_guard lock(_mutex);
                if (const auto previous = _operations.find(id); previous != _operations.end()) {
                    if (previous->second.fingerprint != fingerprint) {
                        Reply(response, {{"error", "request_id reused with different arguments"}}, 409);
                        return;
                    }
                    Reply(response, previous->second.result);
                    return;
                }
                if ((_operations.size() >= 512 || _commands.size() >= 32) && operation != "quit") {
                    Reply(response, {{"error", "Session command budget exhausted"}}, 429);
                    return;
                }
                if (_operations.size() >= 544) {
                    Reply(response, {{"error", "Session shutdown budget exhausted"}}, 429);
                    return;
                }
                nlohmann::json ack = {{"request_id", id}, {"state", "queued"}};
                _operations.emplace(id, Operation {fingerprint, ack});
                _commands.push_back({id, operation, arguments});
                Reply(response, ack, 202);
            }
            catch (const std::exception &error) {
                Reply(response, {{"error", error.what()}}, 400);
            }
        });
        _server->Get("/operation", [this](const auto &request, auto &response) {
            std::lock_guard lock(_mutex);
            const auto operation = _operations.find(request.get_param_value("id"));
            if (operation == _operations.end()) {
                Reply(response, {{"error", "Unknown request"}}, 404);
                return;
            }
            Reply(response, operation->second.result);
        });
        const int port = _server->bind_to_any_port("127.0.0.1");
        if (port < 0)
            throw std::runtime_error("Could not bind QA loopback endpoint");
        _writer   = std::jthread([this](std::stop_token stop) {
            WriteJournal(stop);
        });
        _listener = std::jthread([this] {
            _server->listen_after_bind();
        });
        _server->wait_until_ready();
        if (!_server->is_running())
            throw std::runtime_error("QA loopback listener failed to start");
        std::ofstream endpoint(options.directory / "endpoint.tmp");
        endpoint << nlohmann::json({{"url", "http://127.0.0.1:" + std::to_string(port)}, {"role", options.role}, {"protocol", 1}}).dump(2);
        if (!endpoint)
            throw std::runtime_error("Could not write QA endpoint");
        endpoint.close();
        if (!endpoint)
            throw std::runtime_error("Could not flush QA endpoint");
        std::filesystem::rename(options.directory / "endpoint.tmp", options.directory / "endpoint.json");
        Emit("session.started", {{"capabilities", capabilities}});
    }

    void Session::Stop() {
        if (!_server)
            return;
        _server->stop();
        if (_listener.joinable())
            _listener.join();
        std::vector<std::string> unfinished;
        {
            std::lock_guard lock(_mutex);
            _commands.clear();
            for (const auto &[id, operation] : _operations)
                if (operation.result.at("state") == "queued")
                    unfinished.push_back(id);
        }
        for (const auto &id : unfinished) Complete(id, {{"error", "Session stopped before operation completed"}}, false);
        _writer.request_stop();
        if (_writer.joinable())
            _writer.join();
        _server.reset();
    }

    std::vector<Session::Command> Session::TakeCommands() {
        std::lock_guard lock(_mutex);
        std::vector<Command> commands;
        for (int count = 0; count < 4 && !_commands.empty(); ++count) {
            commands.push_back(std::move(_commands.front()));
            _commands.pop_front();
        }
        return commands;
    }

    void Session::Complete(const std::string &id, const nlohmann::json &result, bool success) {
        {
            std::lock_guard lock(_mutex);
            _operations.at(id).result = {{"request_id", id}, {"state", success ? "completed" : "failed"}, {"tick", Tick()}, {"result", result}};
        }
        Emit(success ? "command.completed" : "command.failed", {{"request_id", id}, {"result", result}});
    }
    void Session::Publish(nlohmann::json snapshot) {
        std::lock_guard lock(_mutex);
        _snapshot = std::move(snapshot);
    }
    bool Session::ContactExpired() const {
        return IsEnabled() && Now() - _lastContact.load(std::memory_order_relaxed) > 120000;
    }

    void Session::Emit(const std::string &name, nlohmann::json payload) {
        if (!IsEnabled())
            return;
        std::lock_guard lock(_mutex);
        nlohmann::json event = {{"sequence", ++_sequence}, {"source", _options.role}, {"tick", Tick()}, {"monotonic_ms", Now()}, {"name", name}, {"payload", std::move(payload)}};
        _events.push_back(event);
        if (_events.size() > 4096)
            _events.pop_front();
        if (_journal.size() < 8192)
            _journal.push_back(std::move(event));
        else
            _journalOverflow = true;
    }

    void Session::WriteJournal(std::stop_token stop) {
        std::ofstream log(_options.directory / "events.jsonl");
        if (!log)
            _journalFailed.store(true);
        for (;;) {
            std::deque<nlohmann::json> batch;
            {
                std::lock_guard lock(_mutex);
                batch.swap(_journal);
            }
            for (const auto &event : batch) log << event.dump() << '\n';
            log.flush();
            if (!log)
                _journalFailed.store(true);
            if (stop.stop_requested()) {
                std::lock_guard lock(_mutex);
                if (_journal.empty())
                    break;
            }
            else
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
} // namespace Framework::Automation
