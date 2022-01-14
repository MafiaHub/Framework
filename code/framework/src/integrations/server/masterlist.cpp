/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "masterlist.h"

#include "instance.h"

namespace Framework::Integrations::Server {
    Masterlist::Masterlist(Instance *instance): _instance(instance) {}

    int32_t Masterlist::GetPlayers() {
        return 0;
    }

    int32_t Masterlist::GetMaxPlayers() {
        return _instance->GetOpts().maxPlayers;
    }

    bool Masterlist::IsPassworded() {
        return !_instance->GetOpts().bindPassword.empty();
    }

    std::string Masterlist::GetVersion() {
        return _instance->GetOpts().modVersion;
    }

    int32_t Masterlist::GetPort() {
        return _instance->GetOpts().bindPort;
    }

    std::string Masterlist::GetHost() {
        return _instance->GetOpts().bindHost;
    }

    std::string Masterlist::GetServerName() {
        return _instance->GetOpts().bindName;
    }

    std::string Masterlist::GetSecretKey() {
        return _instance->GetOpts().bindSecretKey;
    }

    std::string Masterlist::GetMap() {
        return _instance->GetOpts().bindMapName;
    }

    std::string Masterlist::GetGame() {
        return _instance->GetOpts().modName;
    }

} // namespace Framework::Integrations::Server
