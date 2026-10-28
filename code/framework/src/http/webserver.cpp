/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "webserver.h"

#include <httplib.h>
#include <logging/logger.h>

namespace Framework::HTTP {
    Webserver::Webserver() {
        _server = std::make_shared<httplib::Server>();
    }

    Webserver::~Webserver() {
        Shutdown();
    }

    WebserverError Webserver::Init(const std::string &host, int32_t port, const std::string &serveDir) {
        _running  = true;
        _serveDir = serveDir;

        const auto address = (host.empty() ? "0.0.0.0" : host);

        if (!serveDir.empty()) {
            _server->set_mount_point("/", serveDir.c_str());
        }

        _server->set_pre_routing_handler([this](const httplib::Request &req, httplib::Response &res) {
            if (req.method != "GET" && req.method != "HEAD") {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            RequestCallback callback;
            {
                std::lock_guard lock(_getRoutesMutex);
                for (const auto &[pattern, handler] : _getRoutes) {
                    if (std::regex_match(req.path, const_cast<httplib::Request &>(req).matches, pattern)) {
                        callback = handler;
                        break;
                    }
                }
            }
            if (!callback) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            callback(req, res);
            return httplib::Server::HandlerResponse::Handled;
        });

        _server->set_error_handler([](const auto &req, auto &res) {
            res.set_content("Error Status: " + std::to_string(res.status), "text/plain");
        });

        _server->set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
            std::string message;
            try {
                std::rethrow_exception(ep);
            }
            catch (std::exception &e) {
                message = std::string("Internal Server Error: ") + e.what();
            }
            catch (...) {
                message = "Internal Server Error: Unknown Exception";
            }
            res.set_content(message, "text/plain");
            res.status = 500;
        });

        // Bind synchronously so a taken port is reported to the caller
        // instead of failing silently inside the listen thread.
        if (!_server->bind_to_port(address, port)) {
            Logging::GetLogger(FRAMEWORK_INNER_HTTP)->error("[Webserver] Failed to bind to {}:{}", address, port);
            _running = false;
            return WebserverError::WEBSERVER_BIND_FAILED;
        }

        _webThread = std::jthread([this](std::stop_token) {
            _server->listen_after_bind();
        });

        Logging::GetLogger(FRAMEWORK_INNER_HTTP)->debug("[Webserver] Listening on {}:{}", address, port);

        _initialized = true;
        return WebserverError::WEBSERVER_NONE;
    }

    void Webserver::SetWorkerCount(size_t count) {
        _server->new_task_queue = [count] {
            return new httplib::ThreadPool(count);
        };
    }

    void Webserver::Shutdown() {
        if (!_running)
            return;
        _running = false;
        _server->stop();
        if (_webThread.joinable()) {
            _webThread.join();
        }
        Lifecycle::Shutdown();
    }

    void Webserver::RegisterRequest(const std::string &path, const RequestCallback &callback) const {
        if (!_running)
            return;
        if (!path.empty() && callback) {
            std::lock_guard lock(_getRoutesMutex);
            _getRoutes.emplace_back(std::regex(path), callback);
        }
    }

    void Webserver::RegisterPostRequest(const std::string &path, const PostCallback &callback) const {
        if (!_running)
            return;
        if (!path.empty() && callback) {
            _server->Post(path, callback);
        }
    }

    void Webserver::ServeDirectory(const std::string &dir) {
        if (!_running)
            return;
        _serveDir = dir;
        if (!dir.empty())
            _server->set_mount_point("/", dir.c_str());
        else
            _server->remove_mount_point("/");
    }
} // namespace Framework::HTTP
