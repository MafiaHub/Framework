/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>

#include <scripting/utils/table_conversions.h>

namespace Framework::Scripting::Builtins {
    class JSON final {
      public:
        // Convert Lua object to JSON string
        static std::string Stringify(sol::object obj) {
            try {
                nlohmann::json jsonObj = Utils::SolToJson(obj);
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
                return Utils::JsonToSol(s, parsed);
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
