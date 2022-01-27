#pragma once

#include <utils/safe_win32.h>

#include <external/imgui/wrapper.h>
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
        bool _shouldDisplayWidget = true;
        bool _autoScroll          = true;
        bool _isOpen              = false;
        bool _focusOnConsole      = false;
        bool _focusInput          = false;
        bool _consoleControl      = false;
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
