/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "integrations/server/instance.h"

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

MODULE(server_config, {
    using Framework::Integrations::Server::InstanceOptions;

    const auto compiledDefaults = []() {
        InstanceOptions opts;
        opts.modSlug       = "server_config_ut";
        opts.modConfigFile = "server.json";
        opts.bindHost      = "0.0.0.0";
        opts.bindPort      = 27015;
        opts.webBindHost   = "0.0.0.0";
        opts.webBindPort   = 27016;
        opts.bindMapName   = "";
        opts.maxPlayers    = 32;
        opts.bindSecretKey = "";
        return opts;
    };

    // Same order Instance::Init resolves in: the defaults seed the parser, the document overrides
    // them, the command line overrides both.
    const auto resolve = [&compiledDefaults](const nlohmann::json &document, const std::vector<std::string> &args) {
        InstanceOptions opts = compiledDefaults();

        cxxopts::Options options(opts.modSlug, opts.modHelpText);
        Framework::Integrations::Server::AddCommandLineOptions(options, opts);

        std::vector<const char *> argv;
        argv.push_back("server");
        for (const auto &arg : args) {
            argv.push_back(arg.c_str());
        }

        const auto result = options.parse(static_cast<int>(argv.size()), argv.data());
        Framework::Integrations::Server::ApplyConfigDocument(document, opts);
        Framework::Integrations::Server::ApplyCommandLine(result, opts);
        return opts;
    };

    IT("keeps the compiled defaults when neither layer says anything", {
        const auto opts = resolve(nlohmann::json::object(), {});
        STREQUALS(opts.bindHost.c_str(), "0.0.0.0");
        EQUALS(opts.bindPort, 27015);
        EQUALS(opts.webBindPort, 27016);
        EQUALS(opts.maxPlayers, 32);
    });

    IT("lets the config document override a compiled default", {
        const auto opts = resolve({{"host", "127.0.0.1"}, {"port", 20025}, {"apihost", "10.0.0.1"}, {"apiport", 20026}, {"map", "freeride"}, {"maxplayers", 64}, {"server-token", "secret"}}, {});
        STREQUALS(opts.bindHost.c_str(), "127.0.0.1");
        EQUALS(opts.bindPort, 20025);
        STREQUALS(opts.webBindHost.c_str(), "10.0.0.1");
        EQUALS(opts.webBindPort, 20026);
        STREQUALS(opts.bindMapName.c_str(), "freeride");
        EQUALS(opts.maxPlayers, 64);
        STREQUALS(opts.bindSecretKey.c_str(), "secret");
    });

    IT("keeps the compiled default for a key the document omits", {
        const auto opts = resolve({{"maxplayers", 7}}, {});
        EQUALS(opts.maxPlayers, 7);
        STREQUALS(opts.bindHost.c_str(), "0.0.0.0");
        EQUALS(opts.bindPort, 27015);
        EQUALS(opts.webBindPort, 27016);
    });

    IT("does not consume the mod object or unknown keys", {
        const auto opts = resolve({{"port", 20025}, {"mod", {{"season", "winter"}}}, {"unknown", 1}}, {});
        EQUALS(opts.bindPort, 20025);
        STREQUALS(opts.bindHost.c_str(), "0.0.0.0");
    });

    IT("lets the command line override a compiled default", {
        const auto opts = resolve(nlohmann::json::object(), {"-p", "20050", "-h", "127.0.0.1", "-P", "20051", "-H", "127.0.0.2"});
        STREQUALS(opts.bindHost.c_str(), "127.0.0.1");
        EQUALS(opts.bindPort, 20050);
        STREQUALS(opts.webBindHost.c_str(), "127.0.0.2");
        EQUALS(opts.webBindPort, 20051);
    });

    IT("lets the command line override the config document", {
        const auto opts = resolve({{"host", "127.0.0.1"}, {"port", 20025}, {"apiport", 20026}}, {"--port", "20050", "--apiport", "20051"});
        EQUALS(opts.bindPort, 20050);
        EQUALS(opts.webBindPort, 20051);
        STREQUALS(opts.bindHost.c_str(), "127.0.0.1");
    });

    // The regression: a flag that was never passed used to read back its default_value and overwrite
    // whatever the config document had just set.
    IT("leaves the document alone for flags that were not passed", {
        const auto opts = resolve({{"host", "127.0.0.1"}, {"port", 20025}, {"apihost", "10.0.0.1"}, {"apiport", 20026}}, {"-p", "20050"});
        EQUALS(opts.bindPort, 20050);
        STREQUALS(opts.bindHost.c_str(), "127.0.0.1");
        STREQUALS(opts.webBindHost.c_str(), "10.0.0.1");
        EQUALS(opts.webBindPort, 20026);
    });

    // Passing the compiled default explicitly is still an override, so resolution must key off
    // whether the flag appeared, never off comparing its value against the default.
    IT("treats a flag passed with the default value as an override", {
        const auto opts = resolve({{"port", 20025}, {"host", "127.0.0.1"}}, {"-p", "27015", "-h", "0.0.0.0"});
        EQUALS(opts.bindPort, 27015);
        STREQUALS(opts.bindHost.c_str(), "0.0.0.0");
    });

    IT("resolves the config file path from the command line", {
        InstanceOptions opts = compiledDefaults();

        cxxopts::Options options(opts.modSlug, opts.modHelpText);
        Framework::Integrations::Server::AddCommandLineOptions(options, opts);

        const std::vector<const char *> argv = {"server", "-c", "other.json"};
        const auto result                    = options.parse(static_cast<int>(argv.size()), argv.data());

        EQUALS(result.count("config") > 0, true);
        STREQUALS(result["config"].as<std::string>().c_str(), "other.json");
        EQUALS(result.count("port") > 0, false);
    });
});
