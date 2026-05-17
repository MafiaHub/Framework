/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

/*
 * Hello-plugin: a minimal native plugin for the Framework server.
 * Exercises every v1 host capability (logger, commands, HTTP endpoint,
 * player connect/disconnect) and nothing else.
 */

#include "fw_plugin.hpp"

#include <string>

class HelloPlugin final : public Framework::Plugin::Base {
  public:
    int OnInit(Framework::Plugin::Host &host) override {
        _log = host.LoggerFor("hello-plugin");
        _log.Info("hello-plugin loaded");

        host.RegisterCommand("hello", "Print a greeting", [this](int argc, const char *const *argv) {
            std::string who = argc > 1 ? argv[1] : "world";
            _log.Info("hello, " + who);
        });

        host.RegisterHttpEndpoint("/hello", [this](std::string_view /*method*/, std::string_view /*path*/, std::string_view /*body*/, Framework::Plugin::HttpResponse &response) {
            response.SetStatus(200);
            response.SetHeader("Content-Type", "text/plain");
            response.SetBody("hello from the plugin\n");
            _log.Debug("served /hello");
        });

        host.OnPlayerConnect([this](Framework::Plugin::Player &player) {
            _log.Info(player.Nickname() + " (guid=" + std::to_string(player.Guid()) + ") connected");
        });

        host.OnPlayerDisconnect([this](Framework::Plugin::Player &player) {
            _log.Info(player.Nickname() + " (guid=" + std::to_string(player.Guid()) + ") disconnected");
        });

        return 0;
    }

    void OnShutdown(Framework::Plugin::Host & /*host*/) override {
        _log.Info("hello-plugin shutting down");
    }

  private:
    Framework::Plugin::Logger _log;
};

FW_PLUGIN_DECLARE(HelloPlugin, "hello-plugin", "0.1.0")
