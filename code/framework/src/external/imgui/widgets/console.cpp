/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "console.h"

#include <external/imgui/helpers.hpp>
#include <external/optick/wrapper.h>
#include <logging/logger.h>
#include <utils/safe_win32.h>

#include <imgui/imgui.h>

#include <regex>
#include <sstream>

namespace Framework::External::ImGUI::Widgets {
    Console::Console(std::shared_ptr<Utils::CommandProcessor> commandProcessor, std::shared_ptr<Input::IInput> input): _commandProcessor(commandProcessor), _input(input), _logger(Logging::GetLogger("Console").get()) {}

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

        if (_input->IsKeyPressed(FW_KEY_MENU)) {
            if (!_consoleControl) {
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

        auto ringBuffer = Logging::GetInstance()->GetRingBuffer();

        if (_consoleControl) {
            ImVec4 *colors = ImGui::GetStyle().Colors;

            if (colors) {
                ImGui::SetNextWindowBgAlpha(colors[ImGuiCol_WindowBg].w);
            }
        }
        else {
            ImGui::SetNextWindowBgAlpha(_consoleUnfocusedAlpha);
            ImGui::PushFontShadow(0xFF000000);
        }

        auto windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        if (_consoleControl) {
            windowFlags |= ImGuiWindowFlags_MenuBar;
        }
        else {
            windowFlags |= ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        }

        if (!ImGui::Begin("Console", &_shouldDisplayWidget, windowFlags)) {
            ImGui::End();
            return false;
        }

        if (_consoleControl) {
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
                    ImGui::EndMenu();
                }

                for (auto &menuBarDrawer : _menuBarDrawers) {
                    menuBarDrawer();
                }

                ImGui::EndMenuBar();
            }

            ImGui::Checkbox("Auto-scroll", &_autoScroll);
            ImGui::SameLine();
            ImGui::Checkbox("Multi-line", &_isMultiline);
            ImGui::Separator();
        }

        const float reservedHeightMultiline = (_consoleControl && _isMultiline) ? 30.0f : 0.0f;
        const float reservedHeightFocus     = _consoleControl ? 20.0f : 0.0f;
        const float reservedHeight          = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + reservedHeightFocus /* extra space for more items */ + reservedHeightMultiline /* for multiline input */;
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -reservedHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
        if (ringBuffer != nullptr) {
            std::vector<std::string> log_message = ringBuffer->last_formatted();
            for (std::string &log : log_message) {
                FormatLog(log);
            }
        }
        if (_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(0.0f);
        ImGui::EndChild();

        if (_consoleControl) {
            ImGui::Separator();

            static char consoleText[512] = "";
            auto inputEditCallback       = [&](ImGuiInputTextCallbackData *data) {
                if ((_updateInputText || data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) && !_autocompleteWord.empty()) {
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, _autocompleteWord.c_str());

                    ImGui::SetKeyboardFocusHere(-1);
                    _focusOnInput = true;
                }
                else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory && data->EventKey == ImGuiKey_UpArrow) {
                    if (_history.empty())
                        return 0;
                    if (_historyPos == -1) {
                        _tempInputText = data->Buf;
                    }
                    else if (_historyPos + 1 == _history.size()) {
                        return 0;
                    }
                    ++_historyPos;
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, _history.at(_historyPos).c_str());

                    ImGui::SetKeyboardFocusHere(-1);
                    _focusOnInput = true;
                }
                else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory && data->EventKey == ImGuiKey_DownArrow) {
                    if (_historyPos > 0) {
                        --_historyPos;
                        data->DeleteChars(0, data->BufTextLen);
                        data->InsertChars(0, _history.at(_historyPos).c_str());
                    }
                    else if (!_tempInputText.empty()) {
                        _historyPos = -1;
                        data->DeleteChars(0, data->BufTextLen);
                        data->InsertChars(0, _tempInputText.c_str());
                        _tempInputText = "";
                    }

                    ImGui::SetKeyboardFocusHere(-1);
                    _focusOnInput = true;
                }
                return 0;
            };

            auto flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackHistory;

            if (!_consoleControl) {
                // Block input if console is unfocused
                flags = ImGuiInputTextFlags_ReadOnly;
            }
            else if (_updateInputText) {
                flags = ImGuiInputTextFlags_CallbackAlways;
            }

            bool wasInputProcessed;
            if (_isMultiline) {
                wasInputProcessed = ImGui::InputTextMultiline("##console_text", consoleText, 512, {0, 50.0f}, flags, getCallback(inputEditCallback), &inputEditCallback);
            }
            else {
                wasInputProcessed = ImGui::InputText("##console_text", consoleText, 512, flags, getCallback(inputEditCallback), &inputEditCallback);
            }

            if (wasInputProcessed && !_updateInputText) {
                _history.emplace(_history.begin(), consoleText);
                _historyPos = -1;
                SendCommand(consoleText);
                consoleText[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
            if (_focusOnInput) {
                ImGui::SetKeyboardFocusHere(-1);
                _focusOnInput = false;
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

            if (_consoleControl && isAutocompleteOpen && !allCommands.empty() && !commandPreview.empty()) {
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
                            _focusOnInput      = true;
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

                consoleText[0] = '\0';
                _focusOnInput  = true;
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Press ALT to return controls to game or console");

        if (!_consoleControl) {
            ImGui::PopFontShadow();
        }

        ImGui::End();

        return true;
    }

    void Console::SendCommand(const std::string &command) {
        const auto result = _commandProcessor->ProcessCommand(command);

        switch (result.GetError()) {
        case Utils::CommandProcessorError::ERROR_NONE_PRINT_HELP: {
            _logger->info("{}", result.Unwrap());
        } break;
        case Utils::CommandProcessorError::ERROR_CMD_ALREADY_EXISTS: {
            _logger->warn("Command already exists: {}", result.Unwrap());
        } break;
        case Utils::CommandProcessorError::ERROR_CMD_UNSPECIFIED_NAME: {
            _logger->warn("Command name was unspecified");
        } break;
        case Utils::CommandProcessorError::ERROR_CMD_UNKNOWN: {
            _logger->warn("Command not found: {}", result.Unwrap());
        } break;
        case Utils::CommandProcessorError::ERROR_INTERNAL: {
            _logger->warn("Input error: {}", result.Unwrap());
        } break;

        default: break;
        }
    }

    bool Console::Open() {
        LockControls(true);

        _isOpen         = true;
        _focusOnInput   = true;
        _consoleControl = true;

        return true;
    }

    void Console::FormatLog(std::string log) {
        std::regex brackets_regex("\\[.*?\\]", std::regex_constants::ECMAScript);

        auto brackets_begin = std::sregex_iterator(log.begin(), log.end(), brackets_regex);
        auto brackets_end   = std::sregex_iterator();

        int logCount = 1;
        for (std::sregex_iterator i = brackets_begin; i != brackets_end; ++i) {
            const std::smatch &match = *i;
            std::string match_str    = match.str();

            if (logCount == 1) {
                ImGui::TextColored(ImVec4(0.5f, 1, 1, 1), "%s", match_str.c_str());
                ImGui::SameLine();
            }
            if (logCount == 2) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 1, 1), "%s", match_str.c_str());
                ImGui::SameLine();
            }
            if (logCount == 3) {
                if (match_str == "[info]")
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s", match_str.c_str());
                else if (match_str == "[debug]")
                    ImGui::TextColored(ImVec4(0, 0, 1, 1), "%s", match_str.c_str());
                else if (match_str == "[error]")
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", match_str.c_str());
                else if (match_str == "[warning]")
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", match_str.c_str());
                else if (match_str == "[trace]")
                    ImGui::TextColored(ImVec4(0.5f, 0, 1, 1), "%s", match_str.c_str());
                else if (match_str == "[critical]")
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
