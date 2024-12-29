/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "ui_base.h"

#include <utils/command_processor.h>

#include <function2.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <vector>

namespace Framework::External::ImGUI::Widgets {
    class Console: virtual public UIBase {
      public:
        using MenuBarProc = fu2::function<void() const>;

      protected:
        std::shared_ptr<Utils::CommandProcessor> _commandProcessor;

        spdlog::logger *_logger;

        bool _autoScroll = true;

        bool _focusOnInput = false;

        bool _updateInputText = false;

        std::string _tempInputText;

        std::vector<std::string> _history;

        int _historyPos = -1;

        std::string _autocompleteWord;

        std::vector<MenuBarProc> _menuBarDrawers;

        virtual void OnOpen() override;

        virtual void OnClose() override;

        virtual void OnUpdate() override;

        void SendCommand(const std::string &command) const;

        static void FormatLog(std::string log);

      public:
        explicit Console(std::shared_ptr<Utils::CommandProcessor> commandProcessor);

        ~Console() = default;

        void RegisterMenuBarDrawer(const MenuBarProc &proc) {
            _menuBarDrawers.push_back(proc);
        }
    };
} // namespace Framework::External::ImGUI::Widgets
