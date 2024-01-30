/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/input.h>
#include <utils/command_processor.h>

#include <spdlog/spdlog.h>

#include <function2.hpp>
#include <memory>
#include <vector>

namespace Framework::External::ImGUI::Widgets {
    class Console {
      public:
        using MenuBarProc = fu2::function<void() const>;

      protected:
        bool _shouldDisplayWidget    = true;
        bool _autoScroll             = true;
        bool _isOpen                 = false;
        bool _updateInputText        = false;
        bool _focusOnInput           = false;
        bool _isMultiline            = false;
        bool _consoleControl         = false;
        float _consoleUnfocusedAlpha = 0.25f;
        std::string _autocompleteWord;
        std::shared_ptr<Utils::CommandProcessor> _commandProcessor;
        std::shared_ptr<Input::IInput> _input;
        std::vector<MenuBarProc> _menuBarDrawers;
        std::vector<std::string> _history;
        std::string _tempInputText;
        int _historyPos = -1;
        spdlog::logger *_logger;
        static void FormatLog(std::string log);
        void SendCommand(const std::string &command);

      public:
        explicit Console(std::shared_ptr<Utils::CommandProcessor> commandProcessor, std::shared_ptr<Input::IInput> input);
        ~Console() = default;

        void Toggle();
        bool Update();

        bool Open();
        bool Close();

        virtual void LockControls(bool lock) = 0;

        void RegisterMenuBarDrawer(const MenuBarProc &proc) {
            _menuBarDrawers.push_back(proc);
        }

        bool IsOpen() const {
            return _isOpen;
        }
    };
} // namespace Framework::External::ImGUI::Widgets
