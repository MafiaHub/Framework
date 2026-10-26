/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// A launcher shaped like a mod's, compiled and linked against the installed SDK only. Linking it
// covers the whole loader link line the SDK exports (manifest input, game reservation, base address).

#include <launcher/project.h>

int main() {
    Framework::Launcher::ProjectConfiguration config;
    config.name               = "SdkConsumer";
    config.executableName     = L"sdk_consumer_game.exe";
    config.destinationDllName = L"SdkConsumerClient.dll";
    config.classicGamePath    = L".";

    Framework::Launcher::Project project(config);
    return project.GetConfig().name == "SdkConsumer" ? 0 : 1;
}
