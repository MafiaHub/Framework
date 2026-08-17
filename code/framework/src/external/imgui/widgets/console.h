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
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace Framework::External::ImGUI::Widgets {
    class Console: virtual public UIBase {
      public:
        using MenuBarProc = fu2::function<void() const>;

      protected:
        struct LogSegment {
            ImVec4 color;
            bool colored;
            std::string text;
        };
        using LogLine = std::vector<LogSegment>;

        std::shared_ptr<Utils::CommandProcessor> _commandProcessor;

        std::shared_ptr<spdlog::logger> _logger;

        std::vector<LogLine> _cachedLogLines;

        uint64_t _cachedLogEventCount = 0;

        bool _logCacheValid = false;

        bool _autoScroll = true;

        bool _scrollToBottom = false;

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

        void RefreshLogCache(const std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> &ringBuffer);

        static LogLine ParseLog(const std::string &log);

        static void DrawLog(const LogLine &line);

      public:
        explicit Console(std::shared_ptr<Utils::CommandProcessor> commandProcessor);

        ~Console() = default;

        void RegisterMenuBarDrawer(const MenuBarProc &proc) {
            _menuBarDrawers.push_back(proc);
        }
    };
} // namespace Framework::External::ImGUI::Widgets
