/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "command_processor.h"
#include "logging/logger.h"

namespace Framework::Utils {
    Result<std::string, CommandProcessorError> CommandProcessor::ProcessCommand(const std::string &input) {
        std::string command;
        std::vector<std::string> args;
        std::stringstream parts(input);
        std::string item;
        while (std::getline(parts, item, ' ')) {
            args.push_back(item);
        }

        if (args.empty()) {
            return CommandProcessorError::ERROR_EMPTY_INPUT;
        }

        Result<std::string, CommandProcessorError> error = CommandProcessorError::ERROR_NONE;

        command = args[0];

        if (_commands.find(command) != _commands.end()) {
            std::vector<const char *> vArgs;
            for (auto &arg : args) {
                vArgs.push_back(arg.c_str());
            }

            try {
                cxxopts::ParseResult result =
                    _commands[command].options->parse(static_cast<int>(vArgs.size()), vArgs.data());

                if (result.count("help")) {
                    // auto help
                    error = {CommandProcessorError::ERROR_NONE_PRINT_HELP, _commands[command].options->help()};
                } else {
                    _commands[command].proc(result);
                }
            } catch (const std::exception &e) {
                error = {CommandProcessorError::ERROR_INTERNAL, e.what()};
            }
        } else {
            return {CommandProcessorError::ERROR_CMD_UNKNOWN, input};
        }

        return error;
    }

    Result<std::string, CommandProcessorError> CommandProcessor::RegisterCommand(
        const std::string &name, std::initializer_list<cxxopts::Option> options, const CommandProc &proc,
        const std::string &desc) {
        if (name.empty()) {
            return CommandProcessorError::ERROR_CMD_UNSPECIFIED_NAME;
        }
        if (_commands.find(name) != _commands.end()) {
            return {CommandProcessorError::ERROR_CMD_ALREADY_EXISTS, name};
        }

        try {
            auto cmd = std::make_unique<cxxopts::Options>(name, desc);

            if (options.size() > 0)
                cmd->add_options("", options);

            // default help
            cmd->add_option("", {"h,help", "Print usage"});

            _commands[name] = {std::move(cmd), proc};
        } catch (const std::exception &e) {
            return {CommandProcessorError::ERROR_INTERNAL, e.what()};
        }

        return CommandProcessorError::ERROR_NONE;
    }
} // namespace Framework::Utils
