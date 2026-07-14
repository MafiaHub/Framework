/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cxxopts.hpp>

#include "result.h"

#include <function2.hpp>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "hashing.h"

namespace Framework::Utils {
    enum class CommandProcessorError {
        COMMAND_NONE,
        COMMAND_PRINT_HELP,
        COMMAND_EMPTY_INPUT,
        COMMAND_ALREADY_EXISTS,
        COMMAND_UNSPECIFIED_NAME,
        COMMAND_UNKNOWN,
        COMMAND_INTERNAL_ERROR
    };

    using CommandProc = fu2::function<void(cxxopts::ParseResult &) const>;
    class CommandProcessor final {
      private:
        struct CommandInfo {
            std::unique_ptr<cxxopts::Options> options;
            CommandProc proc;
        };
        std::unordered_map<std::string, CommandInfo, StringHash, std::equal_to<>> _commands;

      public:
        CommandProcessor() = default;

        inline std::vector<std::string> GetCommandNames() const {
            std::vector<std::string> names;
            for (auto &cmd : _commands) {
                names.push_back(cmd.first);
            }
            return names;
        }

        inline const CommandInfo *GetCommandInfo(std::string_view name) const {
            auto it = _commands.find(name);
            return it != _commands.end() ? &it->second : nullptr;
        }

        inline void RemoveCommand(std::string_view name) {
            auto it = _commands.find(name);
            if (it != _commands.end()) {
                _commands.erase(it);
            }
        }

        // Split a command line into whitespace-separated tokens, collapsing runs of whitespace. The
        // single tokenizer for every command surface (console, chat), so the same line parses
        // identically wherever it arrives.
        static std::vector<std::string> Tokenize(std::string_view input);

        Result<std::string, CommandProcessorError> ProcessCommand(std::string_view input);
        Result<std::string, CommandProcessorError> RegisterCommand(std::string_view name, std::initializer_list<cxxopts::Option> options, const CommandProc &proc, const std::string &desc = "");
        Result<std::string, CommandProcessorError> RegisterCommand(std::string_view name, const std::vector<cxxopts::Option> &options, const CommandProc &proc, const std::string &desc = "");
    };
} // namespace Framework::Utils
