/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cxxopts.hpp>
#include <spdlog/spdlog.h>

#include "result.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <function2.hpp>

namespace Framework::Utils {
    enum CommandProcessorError {
        ERROR_NONE,
        ERROR_NONE_PRINT_HELP,
        ERROR_EMPTY_INPUT,
        ERROR_CMD_ALREADY_EXISTS,
        ERROR_CMD_UNSPECIFIED_NAME,
        ERROR_CMD_UNKNOWN,
        ERROR_INTERNAL
    };

    using CommandProc = fu2::function<void(cxxopts::ParseResult &) const>;
    class CommandProcessor {
      private:
        struct CommandInfo {
            std::unique_ptr<cxxopts::Options> options;
            CommandProc proc;
        };
        std::unordered_map<std::string, CommandInfo> _commands;

      public:
        CommandProcessor() = default;

        inline std::vector<std::string> GetCommandNames() const {
            std::vector<std::string> names;
            for (auto &cmd : _commands) {
                names.push_back(cmd.first);
            }
            return names;
        }

        inline const CommandInfo *GetCommandInfo(const std::string &name) const {
            return &_commands.at(name);
        }

        Result<std::string, CommandProcessorError> ProcessCommand(const std::string &input);
        Result<std::string, CommandProcessorError> RegisterCommand(const std::string &name, std::initializer_list<cxxopts::Option> options, const CommandProc &proc, const std::string &desc = "");
    };
} // namespace Framework::Utils
