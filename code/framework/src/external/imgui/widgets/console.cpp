/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "console.h"

#include <external/imgui/helpers.hpp>
#include <external/optick/wrapper.h>
#include <logging/logger.h>
#include <utils/safe_win32.h>

#include <fmt/core.h>
#include <imgui/imgui.h>

#include <numeric>
#include <regex>
#include <sstream>

namespace Framework::External::ImGUI::Widgets {
    Console::Console(std::shared_ptr<Framework::Utils::CommandProcessor> commandProcessor): _commandProcessor(commandProcessor), _logger(Framework::Logging::GetLogger("Console").get()) {}

    void Console::Toggle() {
        if (_isOpen)
            Close();
        else
            Open();
    }

    bool Console::Close() {
        LockControls(false);

        _isOpen         = false;
        _consoleControl = false;

        return true;
    }

    bool Console::Update() {
        OPTICK_EVENT();

        if (!_isOpen) {
            return _isOpen;
        }

        if (GetAsyncKeyState(VK_MENU) & 0x1) {
            if (_consoleControl == false) {
                // take back controls
                LockControls(true);
                _consoleControl = true;
            }
            else {
                _consoleControl = false;
                // controls back to game
                LockControls(false);
            }
        }

        auto ringBuffer = Framework::Logging::GetInstance()->GetRingBuffer();

        if (_consoleControl) {
            ImVec4 *colors = ImGui::GetStyle().Colors;

            if (colors) {
                ImGui::SetNextWindowBgAlpha(colors[ImGuiCol_WindowBg].w);
            }
        }
        else {
            ImGui::SetNextWindowBgAlpha(_consoleUnfocusedAlpha);
        }

        if (!ImGui::Begin("Console", &_shouldDisplayWidget, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            return false;
        }

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("Close", "F8")) {
                    Close();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Log Level")) {
                if (ImGui::MenuItem("Trace")) {
                    ringBuffer->set_level(spdlog::level::trace);
                }
                if (ImGui::MenuItem("Debug")) {
                    ringBuffer->set_level(spdlog::level::debug);
                }
                if (ImGui::MenuItem("Info")) {
                    ringBuffer->set_level(spdlog::level::info);
                }
                if (ImGui::MenuItem("Warn")) {
                    ringBuffer->set_level(spdlog::level::warn);
                }
                if (ImGui::MenuItem("Error")) {
                    ringBuffer->set_level(spdlog::level::err);
                }
                if (ImGui::MenuItem("Critical")) {
                    ringBuffer->set_level(spdlog::level::critical);
                }
                if (ImGui::MenuItem("Off")) {
                    ringBuffer->set_level(spdlog::level::off);
                }
                if (ImGui::MenuItem("N")) {
                    ringBuffer->set_level(spdlog::level::n_levels);
                }
                ImGui::EndMenu();
            }

            for (auto &menuBarDrawer : _menuBarDrawers) { menuBarDrawer(); }

            ImGui::EndMenuBar();
        }

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Output");
        ImGui::Checkbox("Auto-scroll", &_autoScroll);
        ImGui::Separator();

        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (ringBuffer != nullptr) {
            std::vector<std::string> log_message = ringBuffer->last_formatted();
            for (std::string &log : log_message) { FormatLog(log); }
        }
        if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(0.0f);
        ImGui::EndChild();

        ImGui::Separator();

        static char consoleText[512] = "";
        auto inputEditCallback       = [&](ImGuiInputTextCallbackData *data) {
            if ((_updateInputText || data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) && !_autocompleteWord.empty()) {
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, _autocompleteWord.c_str());

                ImGui::SetKeyboardFocusHere(-1);
                _focusOnConsole  = true;
            }
            return 0;
        };

        auto flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackResize;

        if (_consoleControl == false) {
            // Block input if console is unfocused
            flags = ImGuiInputTextFlags_ReadOnly;
        }
        else if (_updateInputText) {
            flags = ImGuiInputTextFlags_CallbackAlways;
        }

        if (ImGui::InputText("##console_text", consoleText, 512, flags, getCallback(inputEditCallback), &inputEditCallback) && !_updateInputText) {
            SendCommand(consoleText);
            consoleText[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (_focusOnConsole) {
            ImGui::SetKeyboardFocusHere(-1);
            _focusOnConsole = false;
        }
        ImGui::SameLine();

        // show autocomplete
        static bool isAutocompleteOpen = false;
        std::vector<std::string> allCommands;

        // very ugly: extract command name from the current input
        std::string commandPreview;
        std::stringstream ss(consoleText);
        std::getline(ss, commandPreview, ' ');

        auto commands = _commandProcessor->GetCommandNames();

        for (const auto &command : commands) {
            if (command._Starts_with(commandPreview)) {
                allCommands.push_back(command);
            }
        }

        bool isFocused = ImGui::IsItemFocused();
        isAutocompleteOpen |= ImGui::IsItemActive();
        _autocompleteWord.clear();
        _updateInputText = false;

        if (_consoleControl && isAutocompleteOpen && allCommands.size() > 0 && commandPreview.size() > 0) {
            ImGui::SetNextWindowPos({ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y});
            ImGui::SetNextWindowSize({ImGui::GetItemRectSize().x, 0});
            if (ImGui::Begin("##popup", &isAutocompleteOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_Tooltip)) {
                isFocused |= ImGui::IsWindowFocused();
                std::vector<std::string> foundCommands;
                for (auto &command : allCommands) {
                    if (command._Starts_with(commandPreview) == NULL)
                        continue;

                    foundCommands.push_back(command);
                    auto help                      = _commandProcessor->GetCommandInfo(command)->options->help();
                    const auto formattedSelectable = fmt::format("{} {}", command, help);
                    // TODO ImGui::Selectable(formattedSelectable.c_str(), true)
                    if (ImGui::Selectable(formattedSelectable.c_str()) || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
                        _autocompleteWord  = command;
                        _updateInputText   = true;
                        isAutocompleteOpen = false;
                        _focusOnConsole    = true;
                    }
                }

                if (foundCommands.size() == 1) {
                    _autocompleteWord = foundCommands.at(0) + " ";
                }
                else if (foundCommands.size() > 1) {
                    // find a common prefix among found commands
                    std::string commonPrefix = foundCommands.at(0);
                    for (const auto &cmd : foundCommands) {
                        commonPrefix = commonPrefix.substr(0, std::min(commonPrefix.size(), cmd.size()));
                        for (size_t i = 0; i < commonPrefix.size(); i++) {
                            if (commonPrefix[i] != cmd[i]) {
                                commonPrefix = commonPrefix.substr(0, i);
                                break;
                            }
                        }
                    }
                    _autocompleteWord = commonPrefix;
                }
            }
            ImGui::End();
            isAutocompleteOpen &= isFocused;
        }

        ImGui::SameLine();
        if (ImGui::Button("Send") && _consoleControl) {
            SendCommand(consoleText);

            consoleText[0]  = '\0';
            _focusOnConsole = true;
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Press ALT to return controls to game or console");

        ImGui::End();

        return true;
    }

    void Console::SendCommand(const std::string &command) {
        const auto result = _commandProcessor->ProcessCommand(command);

        switch (result.GetError()) {
        case Framework::Utils::CommandProcessorError::ERROR_NONE_PRINT_HELP: {
            _logger->info("{}", result.Unwrap());
        } break;
        case Framework::Utils::CommandProcessorError::ERROR_EMPTY_INPUT: {
            _logger->warn("Input was empty");
        } break;
        case Framework::Utils::CommandProcessorError::ERROR_CMD_ALREADY_EXISTS: {
            _logger->warn("Command already exists: {}", result.Unwrap());
        } break;
        case Framework::Utils::CommandProcessorError::ERROR_CMD_UNSPECIFIED_NAME: {
            _logger->warn("Command name was unspecified");
        } break;
        case Framework::Utils::CommandProcessorError::ERROR_CMD_UNKNOWN: {
            _logger->warn("Command not found: {}", result.Unwrap());
        } break;
        case Framework::Utils::CommandProcessorError::ERROR_INTERNAL: {
            _logger->warn("Internal error: {}", result.Unwrap());
        } break;

        default: break;
        }
    }

    bool Console::Open() {
        LockControls(true);

        _isOpen         = true;
        _focusOnConsole = true;
        _consoleControl = true;

        return true;
    }

    void Console::FormatLog(std::string log) {
        std::regex brackets_regex("\\[.*?\\]", std::regex_constants::ECMAScript);

        auto brackets_begin = std::sregex_iterator(log.begin(), log.end(), brackets_regex);
        auto brackets_end   = std::sregex_iterator();

        int logCount = 1;
        for (std::sregex_iterator i = brackets_begin; i != brackets_end; ++i) {
            std::smatch match     = *i;
            std::string match_str = match.str();

            if (logCount == 1) {
                ImGui::TextColored(ImVec4(0.5f, 1, 1, 1), "%s", match_str.c_str());
                ImGui::SameLine();
            }
            if (logCount == 2) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 1, 1), "%s", match_str.c_str());
                ImGui::SameLine();
            }
            if (logCount == 3) {
                if (match_str.compare("[info]") == 0)
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", match_str.c_str());
                else if (match_str.compare("[debug]") == 0)
                    ImGui::TextColored(ImVec4(0, 0, 1, 1), "%s", match_str.c_str());
                else if (match_str.compare("[error]") == 0)
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", match_str.c_str());
                else if (match_str.compare("[warning]") == 0)
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", match_str.c_str());
                else if (match_str.compare("[trace]") == 0)
                    ImGui::TextColored(ImVec4(0.5f, 0, 1, 1), "%s", match_str.c_str());
                else if (match_str.compare("[critical]") == 0)
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "%s", match_str.c_str());
                else
                    ImGui::TextColored(ImVec4(1, 0, 0.5f, 1), "%s", match_str.c_str());

                ImGui::SameLine();
                const auto &suffix = match.suffix();
                if (suffix.length() > 0)
                    ImGui::Text("%s", suffix.str().c_str());
            }
            logCount++;
        }
    }
} // namespace Framework::External::ImGUI::Widgets
