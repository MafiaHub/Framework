/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engine.h"

MODULE(scripting_engine, {
    using namespace Framework::Scripting;

    IT("can allocate and deallocate a valid scripting engine instance", {
        Engine *pEngine = new Engine;

        // Init the engine and make sure everything went fine
        EQUALS(pEngine->Init(), EngineError::ENGINE_NONE);
        NEQUALS(pEngine->GetIsolate(), nullptr);
        NEQUALS(pEngine->GetPlatform(), nullptr);
        NEQUALS(pEngine->GetResourceManager(), nullptr);

        // Shutdown the engine and make sure everything went down
        EQUALS(pEngine->Shutdown(), EngineError::ENGINE_NONE);
        EQUALS(pEngine->GetIsolate(), nullptr);
        EQUALS(pEngine->GetPlatform(), nullptr);
        EQUALS(pEngine->GetResourceManager(), nullptr);

        delete pEngine;
    });
})
