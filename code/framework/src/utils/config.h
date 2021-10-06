/*
 * MafiaHub OSS license
 * Copyright (c) 2021 MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>

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
            return (*_document)[field];
        }

        template <typename T>
        void Set(const std::string &field, T value) {
            if (!_lastError.empty())
                return;
            return _document->at(field) = value;
        }
    };
} // namespace Framework::Utils
