/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/command_processor.h>

#include <spdlog/spdlog.h>

#include <functional>
#include <memory>
#include <vector>

namespace Framework::External::ImGUI::Widgets {
    class Console {
      public:
        using MenuBarProc = std::function<void()>;

      protected:
        bool _shouldDisplayWidget     = true;
        bool _autoScroll              = true;
        bool _isOpen                  = false;
        bool _focusOnConsole          = false;
        bool _consoleControl          = false;
        float _consoleUnfocusedAlpha  = 0.25f;
        std::string _autocompleteWord = "";
        std::shared_ptr<Framework::Utils::CommandProcessor> _commandProcessor;
        std::vector<MenuBarProc> _menuBarDrawers;
        spdlog::logger *_logger;
        void FormatLog(std::string log);
        void SendCommand(const std::string &command);

      public:
        Console(std::shared_ptr<Framework::Utils::CommandProcessor> commandProcessor);
        ~Console() = default;

        void Toggle();
        bool Update();

        bool Open();
        bool Close();

        virtual void LockControls(bool lock) = 0;

        void RegisterMenuBarDrawer(MenuBarProc proc) {
            _menuBarDrawers.push_back(proc);
        }

        bool IsOpen() const {
            return _isOpen;
        }
    };
} // namespace Framework::External::ImGUI::Widgets
