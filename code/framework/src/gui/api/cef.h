/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <external/cef/application.h>
#include <string>

#include "api.h"

namespace Framework::GUI {
    class CEF : public API {
      private:
        CefRefPtr<External::CEF::Application> _application;

      public:
        bool Init(const std::string &, bool windowLess = true);
        bool Shutdown();

        void Update();
    };
} // namespace Framework::GUI
