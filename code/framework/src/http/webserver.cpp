#include "webserver.h"

#include <logging/logger.h>

namespace Framework::HTTP {
    void HandleWebRequest(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
        if (ev == MG_EV_HTTP_MSG) {
            auto hm = reinterpret_cast<struct mg_http_message *>(ev_data);

            // Acquire the webserver instance to get back into the scheme
            auto *webServer = (Webserver *)fn_data;

            // Here we loop over registered callbacks. Allows to have multiple handlers for a same endpoint instead of
            // array key
            auto found = false;
            for (auto &it : webServer->GetRegisteredRequestCallbacks()) {
                if (mg_http_match_uri(hm, it.first)) {
                    it.second(c, ev_data);
                    found = true;
                }
            }

            if (!found) {
                const auto serveDir = webServer->GetServeDirectory();
                const auto uri      = std::string(hm->uri.ptr, hm->uri.len);
                if (serveDir.empty()) {
                    Logging::GetLogger(FRAMEWORK_INNER_HTTP)
                        ->trace("[Webserver] Path not registered, sending 404: {}", uri);
                    mg_http_reply(c, 404, "", "");
                } else {
                    struct mg_http_serve_opts opts = {};

                    opts.root_dir = serveDir.c_str();
                    mg_http_serve_dir(c, hm, &opts);
                    Logging::GetLogger(FRAMEWORK_INNER_HTTP)->trace("[Webserver] Serving local files for: {}", uri);
                }
            }
        }
    }

    bool Webserver::Init(const std::string &host, int32_t port) {
        mg_mgr_init(&_manager);
        _running = true;

        auto address = "http://" + (host.empty() ? "0.0.0.0" : host) + ":" + std::to_string(port);
        mg_http_listen(&_manager, address.c_str(), &HandleWebRequest, this);

        _webThread = std::thread([this]() {
            while (this->_running) { mg_mgr_poll(&_manager, 1000); }
        });

        Logging::GetLogger(FRAMEWORK_INNER_HTTP)->debug("[Webserver] Listening on {}", address.c_str());

        _webThread.detach();
        return true;
    }

    bool Webserver::Shutdown() {
        _running = false;
        _webThread.join();
        mg_mgr_free(&_manager);
        return true;
    }

    void Webserver::RegisterRequest(const char *path, RequestCallback callback) {
        if (strlen(path) > 0 && callback) {
            _registeredRequestCallback.insert({path, callback});
            Logging::GetLogger(FRAMEWORK_INNER_HTTP)
                ->debug("[Webserver] Registered request callback for path: {}", path);
        }
    }
    void Webserver::Respond(mg_connection *c, int32_t code, const std::string &message) {
        mg_http_reply(c, code, "", "%s\n", message.c_str());
    }
    void Webserver::ServeDirectory(const std::string &dir) {
        _serveDir = dir;
    }
} // namespace Framework::HTTP
