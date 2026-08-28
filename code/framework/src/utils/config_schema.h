/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Utils {
    enum class ConfigFieldType : uint8_t {
        Bool,
        Int,
        Number,
        String,
        // Arrays and objects, carried as-is. Validated only as "is an array" / "is an object";
        // the mod owns whatever shape lives inside.
        Json,
    };

    // One mod-declared key of server.json's "mod" object.
    struct ConfigField {
        std::string key;
        ConfigFieldType type = ConfigFieldType::String;
        // Also the type contract: a value whose JSON type disagrees with this is a boot error.
        nlohmann::json defaultValue;
        // Absent + required is a boot error. Absent + optional materialises defaultValue, so every
        // read site sees a value and mods do not each reimplement a fallback.
        bool required = false;
        // Off by default. A key reaches clients only when the mod says so, which keeps credentials
        // and internal paths out of the session payload by construction rather than by review.
        bool replicated = false;
        // Optional whitelist, string fields only. Empty means any value of the declared type.
        std::vector<std::string> allowed;
        std::string description;
    };

    using ConfigSchema = std::vector<ConfigField>;

    // Read-only view over the resolved "mod" object. Values are guaranteed present and of the
    // declared type by the time an Instance hands one of these out, so Get() cannot fail for a
    // declared key; an undeclared key returns T{}.
    class ConfigView {
      public:
        explicit ConfigView(const nlohmann::json *values): _values(values) {}

        template <typename T>
        T Get(std::string_view key) const {
            if (!_values) {
                return T {};
            }
            const auto it = _values->find(std::string(key));
            if (it == _values->end()) {
                return T {};
            }
            try {
                return it->get<T>();
            }
            catch (const std::exception &) {
                return T {};
            }
        }

        bool Has(std::string_view key) const {
            return _values && _values->contains(std::string(key));
        }

        const nlohmann::json *Raw() const {
            return _values;
        }

      private:
        const nlohmann::json *_values;
    };

    // Human-readable name of a declared type, for boot diagnostics.
    const char *ConfigFieldTypeName(ConfigFieldType type);

    // True when value's JSON type satisfies the declared type.
    bool ConfigValueMatchesType(const nlohmann::json &value, ConfigFieldType type);

    // Validate and normalise the "mod" object against the schema, in place.
    //
    // Fills in defaults for absent optional keys and leaves undeclared keys untouched, so a config
    // written for a newer mod still loads on an older one. Returns false and fills `error` on the
    // first violation: a missing required key, a type mismatch, or a value outside `allowed`.
    bool ValidateConfigAgainstSchema(const ConfigSchema &schema, nlohmann::json &modObject, std::string &error);

    // The subset of `modObject` whose fields are declared `replicated`, as a fresh object. This is
    // what crosses the wire, so it is built by allow-list rather than by removing known secrets.
    nlohmann::json ExtractReplicatedConfig(const ConfigSchema &schema, const nlohmann::json &modObject);

    // A complete server.json for a first run: framework keys from the caller, mod keys from the
    // schema's defaults. Written only when no config file exists.
    nlohmann::json BuildDefaultConfigDocument(const ConfigSchema &schema, const nlohmann::json &frameworkKeys);
} // namespace Framework::Utils
