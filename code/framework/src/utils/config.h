/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/string_utils.h"

#include <nlohmann/json.hpp>
#include <string>
#include <type_traits>

namespace Framework::Utils {
    class Config {
      private:
        nlohmann::json *_document;
        std::string _lastError;

      public:
        Config();

        ~Config();

        bool Parse(const std::string &content);

        bool IsParsed() const {
            return _lastError.empty();
        }

        std::string ToString();

        nlohmann::json *GetDocument() {
            return _document;
        }

        const std::string &GetLastError() const {
            return _lastError;
        }

        virtual const char *GetDefaultConfig();

        template <typename T>
        T Get(const std::string &field) {
            if (!_lastError.empty())
                return {};
            if constexpr (std::is_same_v<T, std::wstring>) {
                return Utils::StringUtils::NormalToWide((*_document)[field]);
            }
            return (*_document)[field];
        }

        template <typename T>
        T GetDefault(const std::string &field, T defaultValue) {
            if (!_lastError.empty())
                return {};

            try {
                if constexpr (std::is_same_v<T, std::wstring>) {
                    return Utils::StringUtils::NormalToWide((*_document)[field]);
                }
                return (*_document)[field];
            }
            catch (const std::exception &) {
                return defaultValue;
            }
        }

        template <typename T>
        void Set(const std::string &field, T value) {
            if (!_lastError.empty())
                return;
            if constexpr (std::is_same_v<T, std::wstring>) {
                (*_document)[field] = Utils::StringUtils::WideToNormal(value);
                return;
            }
            (*_document)[field] = value;
        }
    };
} // namespace Framework::Utils
