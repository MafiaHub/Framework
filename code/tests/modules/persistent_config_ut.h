/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/persistent_config.h"

// Config::Get/GetDefault/Set are templates that instantiate against nlohmann::json, so this TU
// needs the full definition — persistent_config.h only forward-declares it via json_fwd.hpp.
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

MODULE(persistent_config, {
    using Framework::Utils::PersistentConfig;

    const auto tempPath = (std::filesystem::temp_directory_path() / "fw_persistent_config_ut.json").string();
    std::filesystem::remove(tempPath);

    IT("stays usable when the file does not exist", {
        PersistentConfig config(tempPath);
        EQUALS(config.Load(), false);
        EQUALS(config.IsParsed(), true);
        EQUALS(config.GetDefault<int>("missing", 42), 42);
    });

    IT("round-trips values through Save and Load", {
        {
            PersistentConfig config(tempPath);
            config.Load();
            config.Set<std::string>("host", "192.168.1.10");
            config.Set("port", 27015);
            EQUALS(config.Save(), true);
        }
        PersistentConfig config(tempPath);
        EQUALS(config.Load(), true);
        STREQUALS(config.GetDefault<std::string>("host", "").c_str(), "192.168.1.10");
        EQUALS(config.GetDefault<int>("port", 0), 27015);
    });

    IT("falls back to defaults when the file is corrupt", {
        {
            std::ofstream file(tempPath, std::ios::trunc);
            file << "{not json";
        }
        PersistentConfig config(tempPath);
        EQUALS(config.Load(), false);
        EQUALS(config.IsParsed(), true);
        EQUALS(config.GetDefault<int>("missing", 7), 7);
    });

    std::filesystem::remove(tempPath);
});
