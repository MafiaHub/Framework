/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <type_traits>

#include "utils/string_utils.h"

namespace Framework::Utils {
    using wstring = std::conditional_t<sizeof(wchar_t) == 2, std::u16string, std::u32string>;

    class Config {
      private:
        std::unique_ptr<nlohmann::json> _document;
        std::string _lastError;

        // Logs (once-per-call) that a field was read while the config failed to parse, so the
        // default-on-failure path below is observable instead of silent.
        void LogUnparsedAccess(std::string_view field) const;

      public:
        Config();

        ~Config();

        bool Parse(const std::string &content);

        bool IsParsed() const {
            return _lastError.empty();
        }

        std::string ToString() const;

        nlohmann::json *GetDocument() const {
            return _document.get();
        }

        const std::string &GetLastError() const {
            return _lastError;
        }

        virtual const char *GetDefaultConfig();

        // Document is a template parameter (always nlohmann::json) only so the bodies are checked at
        // instantiation rather than definition, which lets this header get away with json_fwd.hpp.
        // TUs that call Get/GetDefault/Set must include <nlohmann/json.hpp> themselves.
        template <typename T, typename Document = nlohmann::json>
        T Get(std::string_view field) const {
            if (!_lastError.empty()) {
                LogUnparsedAccess(field);
                return {};
            }
            std::string key(field);
            Document &doc = *_document;
            if constexpr (std::is_same_v<T, std::wstring>) {
                const std::string narrow = doc[key];
                return Utils::StringUtils::UTF8ToWide(narrow);
            }
            return doc[key];
        }

        template <typename T, typename Document = nlohmann::json>
        T GetDefault(std::string_view field, T defaultValue) const {
            if (!_lastError.empty()) {
                LogUnparsedAccess(field);
                return defaultValue;
            }

            try {
                std::string key(field);
                Document &doc = *_document;
                if constexpr (std::is_same_v<T, std::wstring>) {
                    const std::string narrow = doc[key];
                    return Utils::StringUtils::UTF8ToWide(narrow);
                }
                return doc[key];
            }
            catch (const std::exception &) {
                return defaultValue;
            }
        }

        template <typename T, typename Document = nlohmann::json>
        void Set(const std::string &field, T value) {
            if (!_lastError.empty())
                return;
            Document &doc = *_document;
            if constexpr (std::is_same_v<T, std::wstring>) {
                doc[field] = Utils::StringUtils::WideToUTF8(value);
                return;
            }
            doc[field] = value;
        }
    };
} // namespace Framework::Utils
