/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "webserver.h"

#include <logging/logger.h>
#include <optick.h>

namespace Framework::HTTP {
    Webserver::Webserver() {
        _server = std::make_shared<httplib::Server>();
    }

    bool Webserver::Init(const std::string &host, int32_t port, const std::string &serveDir) {
        _running  = true;
        _serveDir = serveDir;

        const auto address = (host.empty() ? "0.0.0.0" : host);

        if (!serveDir.empty()) {
            _server->set_mount_point("/", serveDir.c_str());
        }

        _server->set_error_handler([](const auto &req, auto &res) {
            const auto fmt = "<p>Error Status: <span style='color:red;'>%d</span></p>";
            char buf[BUFSIZ];
            snprintf(buf, sizeof(buf), fmt, res.status);
            res.set_content(buf, "text/html");
        });

        _server->set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
            const auto fmt = "<h1>Error 500</h1><p>%s</p>";
            char buf[BUFSIZ];
            try {
                std::rethrow_exception(ep);
            }
            catch (std::exception &e) {
                snprintf(buf, sizeof(buf), fmt, e.what());
            }
            catch (...) { // See the following NOTE
                snprintf(buf, sizeof(buf), fmt, "Unknown Exception");
            }
            res.set_content(buf, "text/html");
            res.status = 500;
        });

        _webThread = std::thread([&]() {
            _server->listen(address, port);
        });

        Logging::GetLogger(FRAMEWORK_INNER_HTTP)->debug("[Webserver] Listening on {}", address.c_str());

        _webThread.detach();
        return true;
    }

    bool Webserver::Shutdown() {
        _running = false;
        if (_webThread.joinable())
            _webThread.join();
        _server->stop();
        return true;
    }

    void Webserver::RegisterRequest(const char *path, const RequestCallback &callback) const {
        if (strlen(path) > 0 && callback) {
            _server->Get(path, callback);
        }
    }

    void Webserver::RegisterPostRequest(const char* path, const PostCallback& callback) const {
        if (strlen(path) > 0 && callback) {
            _server->Post(path, callback);
        }
    }

    void Webserver::ServeDirectory(const std::string &dir) {
        _serveDir = dir;
        if (!dir.empty())
            _server->set_mount_point("/", dir.c_str());
        else
            _server->remove_mount_point("/");
    }
} // namespace Framework::HTTP
