/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "config_schema.h"

namespace Framework::Utils {
    const char *ConfigFieldTypeName(ConfigFieldType type) {
        switch (type) {
        case ConfigFieldType::Bool: return "boolean";
        case ConfigFieldType::Int: return "integer";
        case ConfigFieldType::Number: return "number";
        case ConfigFieldType::String: return "string";
        case ConfigFieldType::Json: return "array or object";
        }
        return "unknown";
    }

    bool ConfigValueMatchesType(const nlohmann::json &value, ConfigFieldType type) {
        switch (type) {
        case ConfigFieldType::Bool: return value.is_boolean();
        case ConfigFieldType::Int: return value.is_number_integer();
        // An integer literal is a valid number; the reverse is not true.
        case ConfigFieldType::Number: return value.is_number();
        case ConfigFieldType::String: return value.is_string();
        case ConfigFieldType::Json: return value.is_array() || value.is_object();
        }
        return false;
    }

    namespace {
        std::string DescribeActualType(const nlohmann::json &value) {
            if (value.is_boolean()) {
                return "boolean";
            }
            if (value.is_number_integer()) {
                return "integer";
            }
            if (value.is_number()) {
                return "number";
            }
            if (value.is_string()) {
                return "string";
            }
            if (value.is_array()) {
                return "array";
            }
            if (value.is_object()) {
                return "object";
            }
            if (value.is_null()) {
                return "null";
            }
            return "unknown";
        }

        std::string JoinAllowed(const std::vector<std::string> &allowed) {
            std::string joined;
            for (size_t i = 0; i < allowed.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += '\'';
                joined += allowed[i];
                joined += '\'';
            }
            return joined;
        }
    } // namespace

    bool ValidateConfigAgainstSchema(const ConfigSchema &schema, nlohmann::json &modObject, std::string &error) {
        if (!modObject.is_object()) {
            error = "'mod' must be an object";
            return false;
        }

        for (const auto &field : schema) {
            const auto it = modObject.find(field.key);

            if (it == modObject.end()) {
                if (field.required) {
                    error = "'mod." + field.key + "' is required";
                    return false;
                }
                // Materialise the default so every read site sees a value.
                modObject[field.key] = field.defaultValue;
                continue;
            }

            if (!ConfigValueMatchesType(*it, field.type)) {
                error = "'mod." + field.key + "' must be a " + ConfigFieldTypeName(field.type) + " (got " + DescribeActualType(*it) + ")";
                return false;
            }

            if (!field.allowed.empty() && field.type == ConfigFieldType::String) {
                const auto value = it->get<std::string>();
                bool permitted = false;
                for (const auto &candidate : field.allowed) {
                    if (candidate == value) {
                        permitted = true;
                        break;
                    }
                }
                if (!permitted) {
                    error = "'mod." + field.key + "' is '" + value + "', expected one of: " + JoinAllowed(field.allowed);
                    return false;
                }
            }
        }

        return true;
    }

    nlohmann::json ExtractReplicatedConfig(const ConfigSchema &schema, const nlohmann::json &modObject) {
        nlohmann::json replicated = nlohmann::json::object();
        if (!modObject.is_object()) {
            return replicated;
        }

        for (const auto &field : schema) {
            if (!field.replicated) {
                continue;
            }
            const auto it = modObject.find(field.key);
            if (it != modObject.end()) {
                replicated[field.key] = *it;
            }
        }
        return replicated;
    }

    nlohmann::json BuildDefaultConfigDocument(const ConfigSchema &schema, const nlohmann::json &frameworkKeys) {
        nlohmann::json document = frameworkKeys.is_object() ? frameworkKeys : nlohmann::json::object();

        nlohmann::json mod = nlohmann::json::object();
        for (const auto &field : schema) {
            mod[field.key] = field.defaultValue;
        }
        document["mod"] = mod;

        return document;
    }
} // namespace Framework::Utils
