/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "config.h"

namespace Framework::Utils {
    // File-backed Config; falls back to GetDefaultConfig() when the file is missing or invalid
    class PersistentConfig final: public Config {
      private:
        std::string _path;

      public:
        explicit PersistentConfig(const std::string &path);

        bool Load();

        bool Save() const;

        const std::string &GetPath() const {
            return _path;
        }
    };
} // namespace Framework::Utils
