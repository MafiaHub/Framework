/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/server_engine.h"

MODULE(scripting_module, {
    using namespace Framework::Scripting;

    IT("can allocate and deallocate a valid scripting engine instance", {
        ServerEngine *pEngine = new ServerEngine;

        // Init the engine and make sure everything went fine
        EQUALS(pEngine->Init(NULL), EngineError::ENGINE_NONE);
        NEQUALS(pEngine->GetLuaEngine().lua_state(), nullptr);

        // Shutdown the engine and make sure everything went down
        EQUALS(pEngine->Shutdown(), EngineError::ENGINE_NONE);
        delete pEngine;
    });
})
