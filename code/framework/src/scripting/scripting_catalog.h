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
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

        inline std::unordered_map<v8::Isolate *, bool> &ScriptingEnvironments() {
            static std::unordered_map<v8::Isolate *, bool> environments;
            return environments;
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

    // Which runtime an isolate belongs to. Set before any builtin registers, so a binding shared by
    // both sides can leave its server-only members off the client's class. Unset reads as server.
    inline void SetScriptingEnvironment(v8::Isolate *isolate, bool isClient) {
        if (!isolate) {
            throw std::invalid_argument("SetScriptingEnvironment requires an isolate");
        }

        std::scoped_lock lock(detail::ScriptingCatalogMutex());
        detail::ScriptingEnvironments()[isolate] = isClient;
    }

    inline bool IsClientScripting(v8::Isolate *isolate) {
        if (!isolate) {
            return false;
        }

        std::scoped_lock lock(detail::ScriptingCatalogMutex());
        const auto &environments = detail::ScriptingEnvironments();
        const auto it            = environments.find(isolate);
        return it != environments.end() && it->second;
    }

    // Adds the symbols `source` defines and `destination` does not, so a project exporting its
    // own catalog carries the framework's builtins with it -- Chat, Key, Voice and the rest
    // register into a catalog of their own, and a project's metadata file would otherwise
    // document only half its globals.
    //
    // A class a project also defines is not blended: both sides define Player, and merging their
    // members produces an interface extending two types that declare the same property
    // differently, which is not expressible in TypeScript. The project's definition is the
    // specialised one, so it wins the name -- and the framework's is carried across as
    // `Base<Name>`, because the project's class inherits it at runtime and says so in its bases.
    // Extending it is exactly what the project's declaration wants; only merging the members into
    // one symbol was ever the problem.
    //
    // A data type both sides define is blended property by property instead, because its members
    // are independent rather than a redefinition of each other. EventMap is the case that matters:
    // a project creates it for its own events, and the framework's belong in the same map. Without
    // this the framework cannot document an event it raises itself, and every mod has to restate
    // the whole set by hand. A property the project already declares still wins.
    //
    // `skip` drops source symbols by name, for globals a project documents through some other
    // shape than the framework's own -- the event bus is emitted as Core.Events of type
    // EventBus, so carrying the framework's Events object across would declare it twice.
    inline void MergeScriptingCatalog(v8pp::metadata::registry &destination, const v8pp::metadata::registry &source, std::initializer_list<std::string_view> skip = {}) {
        // The destination's symbol of this name, or nullptr. Read-only: adding through the registry
        // is what hands back a mutable one.
        const auto existing = [&destination](const std::string &name) -> const v8pp::metadata::symbol * {
            const auto &symbols = destination.symbols();
            const auto it       = std::find_if(symbols.begin(), symbols.end(), [&name](const v8pp::metadata::symbol &candidate) {
                return candidate.name == name;
            });
            return it != symbols.end() ? &*it : nullptr;
        };

        const auto declares = [](const v8pp::metadata::symbol &symbol, const std::string &name) {
            return std::any_of(symbol.properties.begin(), symbol.properties.end(), [&name](const v8pp::metadata::property &property) {
                return property.name == name;
            });
        };

        // Classes carried across under a documentation name, as {registered name, documented name}.
        // Applied after the whole merge rather than at the rename, so a source symbol imported later
        // and naming the same base is repointed too.
        std::vector<std::pair<std::string, std::string>> renames;

        for (const auto &symbol : source.symbols()) {
            if (std::find(skip.begin(), skip.end(), symbol.name) != skip.end()) {
                continue;
            }

            if (const v8pp::metadata::symbol *collision = existing(symbol.name)) {
                const bool blendable = symbol.kind == v8pp::metadata::symbol_kind::data_type && collision->kind == v8pp::metadata::symbol_kind::data_type;
                if (!blendable) {
                    // A class both sides define still has to reach the output, because the project's
                    // class inherits it at runtime and records that inheritance as a base. Dropping it
                    // leaves that base naming the project's own class -- a declaration extending
                    // itself. Carry it across under a documentation name instead; the rename is
                    // repointed into every base that named it once the merge is done.
                    if (symbol.kind != v8pp::metadata::symbol_kind::constructor || collision->kind != v8pp::metadata::symbol_kind::constructor) {
                        continue;
                    }
                    std::string documented = "Base" + symbol.name;
                    while (existing(documented) != nullptr) {
                        documented += "_";
                    }
                    v8pp::metadata::symbol &renamed = destination.constructor(documented, symbol.description);
                    renamed.constructor            = symbol.constructor;
                    renamed.functions              = symbol.functions;
                    renamed.properties             = symbol.properties;
                    renamed.bases                  = symbol.bases;
                    renames.emplace_back(symbol.name, documented);
                    continue;
                }
                // Safe to add through the registry now that the kinds are known to agree; it
                // returns the symbol already there rather than a second one.
                v8pp::metadata::symbol &target = destination.data_type(symbol.name);
                for (const auto &property : symbol.properties) {
                    if (!declares(target, property.name)) {
                        target.record(property);
                    }
                }
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

        // Repoint the renames. Collected first and mutated after, because `symbols()` is read-only and
        // the registry hands back a mutable symbol only by name and kind.
        for (const auto &[registered, documented] : renames) {
            std::vector<std::pair<std::string, v8pp::metadata::symbol_kind>> naming;
            for (const auto &candidate : destination.symbols()) {
                if (candidate.name != documented && std::find(candidate.bases.begin(), candidate.bases.end(), registered) != candidate.bases.end()) {
                    naming.emplace_back(candidate.name, candidate.kind);
                }
            }

            for (const auto &[name, kind] : naming) {
                v8pp::metadata::symbol *target = nullptr;
                switch (kind) {
                case v8pp::metadata::symbol_kind::global_object: target = &destination.global_object(name); break;
                case v8pp::metadata::symbol_kind::constructor: target = &destination.constructor(name); break;
                case v8pp::metadata::symbol_kind::data_type: target = &destination.data_type(name); break;
                }

                if (target) {
                    std::replace(target->bases.begin(), target->bases.end(), registered, documented);
                }
            }
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
        detail::ScriptingEnvironments().erase(isolate);
    }
} // namespace Framework::Scripting
