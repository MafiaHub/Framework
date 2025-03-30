/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>
#include <nlohmann/json.hpp>

namespace Framework::Scripting::Builtins {
    class JSON final {
      private:
        // Convert sol::object to nlohmann::json
        static nlohmann::json SolToJson(const sol::object &obj) {
            if (obj.is<sol::nil_t>()) {
                return nullptr;
            }
            else if (obj.is<bool>()) {
                return obj.as<bool>();
            }
            else if (obj.is<std::string>()) {
                return obj.as<std::string>();
            }
            else if (obj.is<int>()) {
                return obj.as<int>();
            }
            else if (obj.is<double>()) {
                return obj.as<double>();
            }
            else if (obj.is<sol::table>()) {
                sol::table table = obj.as<sol::table>();
                
                // Check if it's an array
                bool isArray = true;
                for (auto const &pair : table) {
                    if (!pair.first.is<int>() || pair.first.as<int>() <= 0) {
                        isArray = false;
                        break;
                    }
                }
                
                if (isArray) {
                    nlohmann::json arr = nlohmann::json::array();
                    for (int i = 1; i <= table.size(); i++) {
                        sol::object value = table[i];
                        arr.push_back(SolToJson(value));
                    }
                    return arr;
                }
                else {
                    nlohmann::json obj = nlohmann::json::object();
                    for (auto const &pair : table) {
                        if (pair.first.is<std::string>()) {
                            std::string key = pair.first.as<std::string>();
                            sol::object value = pair.second;
                            obj[key] = SolToJson(value);
                        }
                    }
                    return obj;
                }
            }
            
            // Default case for unsupported types
            return nullptr;
        }
        
        // Convert nlohmann::json to sol::object
        static sol::object JsonToSol(sol::this_state s, const nlohmann::json &json) {
            if (json.is_null()) {
                return sol::nil;
            }
            else if (json.is_boolean()) {
                return sol::make_object(s, json.get<bool>());
            }
            else if (json.is_string()) {
                return sol::make_object(s, json.get<std::string>());
            }
            else if (json.is_number_integer()) {
                return sol::make_object(s, json.get<int>());
            }
            else if (json.is_number_float()) {
                return sol::make_object(s, json.get<double>());
            }
            else if (json.is_array()) {
                sol::state_view lua(s);
                sol::table arr = lua.create_table();
                for (size_t i = 0; i < json.size(); i++) {
                    arr[i + 1] = JsonToSol(s, json[i]);
                }
                return arr;
            }
            else if (json.is_object()) {
                sol::state_view lua(s);
                sol::table obj = lua.create_table();
                for (auto it = json.begin(); it != json.end(); ++it) {
                    obj[it.key()] = JsonToSol(s, it.value());
                }
                return obj;
            }
            
            // Default case
            return sol::nil;
        }
        
      public:
        // Convert Lua object to JSON string
        static std::string Stringify(sol::object obj) {
            try {
                nlohmann::json jsonObj = SolToJson(obj);
                return jsonObj.dump();
            }
            catch (const std::exception &e) {
                return std::string("Error in JSON.stringify: ") + e.what();
            }
        }
        
        // Parse JSON string to Lua object
        static sol::object Parse(sol::this_state s, const std::string &jsonStr) {
            try {
                nlohmann::json parsed = nlohmann::json::parse(jsonStr);
                return JsonToSol(s, parsed);
            }
            catch (const std::exception &e) {
                return sol::make_object(s, nullptr);
            }
        }
        
        static void Register(sol::state *luaEngine) {
            sol::usertype<JSON> cls = luaEngine->new_usertype<JSON>("JSON");
            
            // Register static functions
            cls.set_function("stringify", &JSON::Stringify);
            cls.set_function("parse", &JSON::Parse);
        }
    };
} // namespace Framework::Scripting::Builtins 