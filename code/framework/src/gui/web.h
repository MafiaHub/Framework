#pragma once

#include <integrations/cef/application.h>
#include <string>

namespace Framework::GUI {
    class Web {
      private:
        CefRefPtr<Integrations::CEF::Application> _application;

      public:
        bool Init(const std::string &, bool windowLess = true);
        bool Shutdown();

        void Update();
    };
} // namespace Framework::GUI
