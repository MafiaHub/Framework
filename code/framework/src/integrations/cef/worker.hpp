#pragma once

#include "application.h"

#ifdef _WIN32
#include <windows.h>
#include <delayimp.h>
#endif

#ifdef __APPLE__
#include <wrapper/cef_library_loader.h>
#endif

namespace Framework::Integrations::CEF {
    #ifdef _WIN32
        int CALLBACK WorkerMain(
            _In_ HINSTANCE hInstance,
            _In_ HINSTANCE hPrevInstance,
            _In_ LPSTR     lpCmdLine,
            _In_ int       nCmdShow
        )
    #else
        int WorkerMain(int argc, char* argv[])
    #endif
    {
        #ifdef __APPLE__
            // Load the CEF framework library at runtime instead of linking directly
            // as required by the macOS sandbox implementation.
            CefScopedLibraryLoader libraryLoader;
            if (!libraryLoader.LoadInHelper())
                return 1;
        #endif

        #ifdef _WIN32
            CefMainArgs mainArgs(hInstance);
        #else
            CefMainArgs mainArgs(argc, argv);
        #endif

        CefRefPtr<Application> app = new Application;
        return CefExecuteProcess(mainArgs, app, nullptr);
    }
}