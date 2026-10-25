/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// A client DLL shaped like a mod's, compiled and linked against the installed SDK only.

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>
#include <logging/logger.h>
#include <utils/hooking/hooking.h>

namespace {
    class Client final : public Framework::Integrations::Client::Instance {
      public:
        void PostInit() override {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("SDK consumer client ready");
        }
    };
} // namespace

extern "C" __declspec(dllexport) int SdkConsumerEntry(HWND windowHandle) {
    Framework::Integrations::Client::InstanceOptions opts;
    opts.gameName               = "SDK";
    opts.gameVersion            = "1.0.0";
    opts.modSlug                = "sdk_consumer";
    opts.modVersion             = "0.0.1";
    opts.useRenderer            = false;
    opts.useImGUI               = false;
    opts.usePresence            = false;
    opts.rpcIdentifierSalt      = 0x5DC0FFEEULL;
    opts.rendererOptions.windowHandle = windowHandle;

    Client client;
    return 0;
}
