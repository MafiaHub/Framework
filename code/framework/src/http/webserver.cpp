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
#include <metrics/registry.h>

#include <chrono>

namespace Framework::HTTP {
    namespace {
        Metrics::Counter *RouteStatusCounter(const std::string &route, const char *codeClass) {
            return Metrics::Registry::Get().RegisterCounter("fw_http_requests_total", "HTTP requests by route and status class", {{"route", route}, {"code", codeClass}});
        }

        struct RouteMetrics {
            Metrics::Counter *c2xx       = nullptr;
            Metrics::Counter *c4xx       = nullptr;
            Metrics::Counter *c5xx       = nullptr;
            Metrics::Counter *cother     = nullptr;
            Metrics::Histogram *duration = nullptr;
            Metrics::Gauge *inFlight     = nullptr;
        };

        RouteMetrics MakeRouteMetrics(const std::string &route) {
            auto &reg = Metrics::Registry::Get();
            RouteMetrics metrics {
                RouteStatusCounter(route, "2xx"),
                RouteStatusCounter(route, "4xx"),
                RouteStatusCounter(route, "5xx"),
                RouteStatusCounter(route, "other"),
                reg.RegisterHistogram("fw_http_request_duration_seconds", "HTTP route handler duration", Metrics::Buckets::Exponential(0.0005, 2.0, 13), {{"route", route}}),
                reg.RegisterGauge("fw_http_requests_in_flight", "HTTP requests currently executing a route handler", {{"route", route}}),
            };
            metrics.inFlight->Set(0.0);
            return metrics;
        }

        void RecordRouteStatus(const RouteMetrics &c, int status) {
            Metrics::Counter *sel = status >= 200 && status < 300 ? c.c2xx : status >= 400 && status < 500 ? c.c4xx : status >= 500 && status < 600 ? c.c5xx : c.cother;
            if (sel) {
                sel->Inc();
            }
        }

        int EffectiveStatus(const httplib::Request &req, const httplib::Response &res) {
            if (res.status != -1) {
                return res.status;
            }
            return req.ranges.empty() ? 200 : 206;
        }

        void FinishRoute(const RouteMetrics &metrics, std::chrono::steady_clock::time_point startedAt, int status) {
            RecordRouteStatus(metrics, status);
            if (metrics.duration) {
                metrics.duration->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count());
            }
            if (metrics.inFlight) {
                metrics.inFlight->Add(-1.0);
            }
        }
    }

    Webserver::Webserver() {
        _server = std::make_shared<httplib::Server>();
    }

    WebserverError Webserver::Init(const std::string &host, int32_t port, const std::string &serveDir) {
        _running  = true;
        _serveDir = serveDir;

        const auto address = (host.empty() ? "0.0.0.0" : host);

        if (!serveDir.empty()) {
            _server->set_mount_point("/", serveDir.c_str());
        }

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
        if (path.empty() || !callback)
            return;

        const RouteMetrics metrics = MakeRouteMetrics(path);
        RequestCallback userCb     = callback;
        auto wrapped               = [userCb, metrics](const httplib::Request &req, httplib::Response &res) {
            const auto startedAt = std::chrono::steady_clock::now();
            if (metrics.inFlight) {
                metrics.inFlight->Add(1.0);
            }
            try {
                userCb(req, res);
                FinishRoute(metrics, startedAt, EffectiveStatus(req, res));
            }
            catch (...) {
                FinishRoute(metrics, startedAt, 500);
                throw;
            }
        };
        _server->Get(path, wrapped);
    }

    void Webserver::RegisterPostRequest(const std::string &path, const PostCallback &callback) const {
        if (!_running)
            return;
        if (path.empty() || !callback)
            return;

        const RouteMetrics metrics = MakeRouteMetrics(path);
        PostCallback userCb        = callback;
        auto wrapped               = [userCb, metrics](const httplib::Request &req, httplib::Response &res, const httplib::ContentReader &reader) {
            const auto startedAt = std::chrono::steady_clock::now();
            if (metrics.inFlight) {
                metrics.inFlight->Add(1.0);
            }
            try {
                userCb(req, res, reader);
                FinishRoute(metrics, startedAt, EffectiveStatus(req, res));
            }
            catch (...) {
                FinishRoute(metrics, startedAt, 500);
                throw;
            }
        };
        _server->Post(path, wrapped);
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
