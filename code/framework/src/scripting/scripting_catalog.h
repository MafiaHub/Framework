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

#include <algorithm>
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

    // Adds the symbols `source` defines and `destination` does not, so a project exporting its
    // own catalog carries the framework's builtins with it -- Chat, Key, Voice and the rest
    // register into a catalog of their own, and a project's metadata file would otherwise
    // document only half its globals.
    //
    // A name the project already defines is skipped whole rather than blended: both sides
    // define Player, and merging their members produces an interface extending two types that
    // declare the same property differently, which is not expressible in TypeScript. The
    // project's definition is the specialised one, so it wins.
    // `skip` drops source symbols by name, for globals a project documents through some other
    // shape than the framework's own -- the event bus is emitted as Core.Events of type
    // EventBus, so carrying the framework's Events object across would declare it twice.
    inline void MergeScriptingCatalog(v8pp::metadata::registry &destination, const v8pp::metadata::registry &source, std::initializer_list<std::string_view> skip = {}) {
        const auto defines = [&destination, &skip](const std::string &name) {
            if (std::find(skip.begin(), skip.end(), name) != skip.end()) {
                return true;
            }

            const auto &symbols = destination.symbols();
            return std::any_of(symbols.begin(), symbols.end(), [&name](const v8pp::metadata::symbol &existing) {
                return existing.name == name;
            });
        };

        for (const auto &symbol : source.symbols()) {
            if (defines(symbol.name)) {
                continue;
            }

            v8pp::metadata::symbol *target = nullptr;
            switch (symbol.kind) {
            case v8pp::metadata::symbol_kind::global_object: target = &destination.global_object(symbol.name, symbol.description); break;
            case v8pp::metadata::symbol_kind::constructor: target = &destination.constructor(symbol.name, symbol.description); break;
            case v8pp::metadata::symbol_kind::data_type: target = &destination.data_type(symbol.name, symbol.description); break;
            }

            if (!target) {
                continue;
            }

            target->constructor = symbol.constructor;
            target->functions   = symbol.functions;
            target->properties  = symbol.properties;
            target->bases       = symbol.bases;
        }

        const auto &existingVariables = destination.variables();
        for (const auto &variable : source.variables()) {
            const bool present = std::any_of(existingVariables.begin(), existingVariables.end(), [&variable](const v8pp::metadata::variable &existing) {
                return existing.name == variable.name;
            });
            if (!present) {
                destination.variable_(variable.name, variable.value_type, variable.description, variable.readonly);
            }
        }
    }

    inline void ClearScriptingCatalog(v8::Isolate *isolate) {
        if (!isolate) {
            return;
        }

        std::scoped_lock lock(detail::ScriptingCatalogMutex());
        detail::ScriptingCatalogs().erase(isolate);
    }
} // namespace Framework::Scripting
