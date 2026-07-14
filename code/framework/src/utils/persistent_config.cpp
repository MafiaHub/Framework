/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "persistent_config.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <logging/logger.h>

namespace Framework::Utils {
    PersistentConfig::PersistentConfig(const std::string &path): _path(path) {
    }

    bool PersistentConfig::Load() {
        std::ifstream file(_path);
        if (!file.is_open()) {
            Parse(GetDefaultConfig());
            return false;
        }

        std::stringstream content;
        content << file.rdbuf();

        if (!Parse(content.str())) {
            Logging::GetLogger("Config")->warn("Failed to parse '{}', using defaults: {}", _path, GetLastError());
            Parse(GetDefaultConfig());
            return false;
        }
        return true;
    }

    bool PersistentConfig::Save() const {
        if (!IsParsed()) {
            return false;
        }

        std::error_code ec;
        const auto parent = std::filesystem::path(_path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }

        std::ofstream file(_path, std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file << GetDocument()->dump(4);
        return file.good();
    }
} // namespace Framework::Utils
