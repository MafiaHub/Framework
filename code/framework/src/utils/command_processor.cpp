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
    std::vector<std::string> CommandProcessor::Tokenize(std::string_view input) {
        std::vector<std::string> tokens;
        std::istringstream parts{std::string(input)};
        std::string item;
        while (parts >> item) {
            tokens.push_back(item);
        }
        return tokens;
    }

    Result<std::string, CommandProcessorError> CommandProcessor::ProcessCommand(std::string_view input) {
        std::string command;
        std::vector<std::string> args = Tokenize(input);

        if (args.empty()) {
            return CommandProcessorError::COMMAND_EMPTY_INPUT;
        }

        Result<std::string, CommandProcessorError> error = CommandProcessorError::COMMAND_NONE;

        command = args[0];

        if (_commands.contains(command)) {
            std::vector<const char *> vArgs;
            for (auto &arg : args) {
                vArgs.push_back(arg.c_str());
            }

            try {
                cxxopts::ParseResult result = _commands[command].options->parse(static_cast<int>(vArgs.size()), vArgs.data());

                if (result.count("help")) {
                    // auto help
                    error = {CommandProcessorError::COMMAND_PRINT_HELP, _commands[command].options->help()};
                }
                else {
                    _commands[command].proc(result);
                }
            }
            catch (const std::exception &e) {
                error = {CommandProcessorError::COMMAND_INTERNAL_ERROR, e.what()};
            }
        }
        else {
            return {CommandProcessorError::COMMAND_UNKNOWN, std::string(input)};
        }

        return error;
    }

    Result<std::string, CommandProcessorError> CommandProcessor::RegisterCommand(std::string_view name, std::initializer_list<cxxopts::Option> options, const CommandProc &proc, const std::string &desc) {
        if (name.empty()) {
            return CommandProcessorError::COMMAND_UNSPECIFIED_NAME;
        }
        if (_commands.contains(name)) {
            return {CommandProcessorError::COMMAND_ALREADY_EXISTS, std::string(name)};
        }

        try {
            std::string nameStr(name);
            auto cmd = std::make_unique<cxxopts::Options>(nameStr, desc);

            if (options.size() > 0)
                cmd->add_options("", options);

            // default help
            cmd->add_option("", {"h,help", "Print usage"});

            _commands[nameStr] = {std::move(cmd), proc};
        }
        catch (const std::exception &e) {
            return {CommandProcessorError::COMMAND_INTERNAL_ERROR, e.what()};
        }

        return CommandProcessorError::COMMAND_NONE;
    }

    Result<std::string, CommandProcessorError> CommandProcessor::RegisterCommand(std::string_view name, const std::vector<cxxopts::Option> &options, const CommandProc &proc, const std::string &desc) {
        if (name.empty()) {
            return CommandProcessorError::COMMAND_UNSPECIFIED_NAME;
        }
        if (_commands.contains(name)) {
            return {CommandProcessorError::COMMAND_ALREADY_EXISTS, std::string(name)};
        }

        try {
            std::string nameStr(name);
            auto cmd = std::make_unique<cxxopts::Options>(nameStr, desc);

            if (!options.empty()) {
                for (const auto &option : options) {
                    cmd->add_option("", option);
                }
            }

            // default help
            cmd->add_option("", {"h,help", "Print usage"});

            _commands[nameStr] = {std::move(cmd), proc};
        }
        catch (const std::exception &e) {
            return {CommandProcessorError::COMMAND_INTERNAL_ERROR, e.what()};
        }

        return CommandProcessorError::COMMAND_NONE;
    }
} // namespace Framework::Utils
