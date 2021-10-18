/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "cef.h"

#include <logging/logger.h>

namespace Framework::GUI {
    bool CEF::Init(const std::string &rootPath, bool windowLess) {
        // Create the application instance
        _application = new External::CEF::Application();

// Prepare the launch arguments
// TODO: make it UNIX compatible
#ifndef WIN32
        CefMainArgs args(0, nullptr);
#else
        CefMainArgs args(0);
#endif

        // Prepare the settings
        CefSettings settings;
        CefString(&settings.resources_dir_path)      = rootPath + "\\cef";
        CefString(&settings.log_file)                = rootPath + "\\logs\\cef.txt";
        CefString(&settings.locales_dir_path)        = rootPath + "\\cef\\locales";
        CefString(&settings.cache_path)              = rootPath + "\\cef\\cache";
        CefString(&settings.user_data_path)          = rootPath + "\\cef\\userdata";
        CefString(&settings.browser_subprocess_path) = rootPath + "\\CefWorker.exe"; // TODO: make it UNIX compatible
        settings.multi_threaded_message_loop         = false;
        settings.log_severity                        = LOGSEVERITY_WARNING;
        settings.remote_debugging_port               = 7777;
        settings.windowless_rendering_enabled        = windowLess;
        settings.background_color                    = 0x00000000;

        // Initialize the app
        if (!CefInitialize(args, settings, _application, nullptr)) {
            // TODO: log it out
            return false;
        }

        // Register the custom URI schemes
        // TODO: implement

        return true;
    }

    bool CEF::Shutdown() {
        if (!_application) {
            return false;
        }

        CefShutdown();
        return true;
    }

    void CEF::Update() {
        if (!_application) {
            return;
        }

        CefDoMessageLoopWork();
    }
} // namespace Framework::GUI
