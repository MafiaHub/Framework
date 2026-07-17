/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/metadata.hpp>

#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace Framework::Scripting {
    namespace detail {
        inline std::mutex &ScriptingCatalogMutex() {
            static std::mutex mutex;
            return mutex;
        }

        inline std::unordered_map<v8::Isolate *, v8pp::metadata::registry *> &ScriptingCatalogs() {
            static std::unordered_map<v8::Isolate *, v8pp::metadata::registry *> catalogs;
            return catalogs;
        }
    } // namespace detail

    inline void SetScriptingCatalog(v8::Isolate *isolate, std::string_view name) {
        if (!isolate) {
            throw std::invalid_argument("SetScriptingCatalog requires an isolate");
        }

        std::scoped_lock lock(detail::ScriptingCatalogMutex());
        detail::ScriptingCatalogs()[isolate] = &v8pp::metadata::catalog(name);
    }

    inline v8pp::metadata::registry &GetScriptingCatalog(v8::Isolate *isolate) {
        if (!isolate) {
            throw std::invalid_argument("GetScriptingCatalog requires an isolate");
        }

        std::scoped_lock lock(detail::ScriptingCatalogMutex());
        const auto &catalogs = detail::ScriptingCatalogs();
        const auto it        = catalogs.find(isolate);
        return it == catalogs.end() ? v8pp::metadata::catalog("framework") : *it->second;
    }

    inline void ClearScriptingCatalog(v8::Isolate *isolate) {
        if (!isolate) {
            return;
        }

        std::scoped_lock lock(detail::ScriptingCatalogMutex());
        detail::ScriptingCatalogs().erase(isolate);
    }
} // namespace Framework::Scripting
