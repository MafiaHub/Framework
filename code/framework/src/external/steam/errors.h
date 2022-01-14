/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::External::Steam {
    enum class SteamError { STEAM_NONE, STEAM_CLIENT_NOT_RUNNING, STEAM_INIT_FAILED, STEAM_CONTEXT_CREATION_FAILED, STEAM_CONTEXT_INIT_FAILED, STEAM_USER_NOT_LOGGED_ON };
}
