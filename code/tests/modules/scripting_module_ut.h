/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engines/node/engine.h"
#include "scripting/module.h"

MODULE(scripting_module, {
    using namespace Framework::Scripting;

    IT("should not allow to initialize the engine with an invalid type", {
        Module *pEngine = new Module;
        EQUALS(pEngine->Init(static_cast<EngineTypes>(-1), NULL), ModuleError::MODULE_ENGINE_NULL);
        delete pEngine;
    });

    IT("should not allow to shutdown if the engine is not initialized", {
        Module *pEngine = new Module;
        EQUALS(pEngine->Shutdown(), ModuleError::MODULE_ENGINE_NULL);
        delete pEngine;
    });

    IT("should not allow to load gamemode if the engine is not initialized", {
        Module *pEngine = new Module;
        EQUALS(pEngine->LoadGamemode(), false);
        delete pEngine;
    });

    IT("should not allow to unload gamemode if the engine is not initialized", {
        Module *pEngine = new Module;
        EQUALS(pEngine->UnloadGamemode(), false);
        delete pEngine;
    });

    IT("can allocate and deallocate a valid scripting engine instance", {
        Module *pEngine = new Module;

        // Init the engine and make sure everything went fine
        EQUALS(pEngine->Init(EngineTypes::ENGINE_NODE, NULL), ModuleError::MODULE_NONE);
        NEQUALS(pEngine->GetEngine(), nullptr);
        NEQUALS(reinterpret_cast<Engines::Node::Engine *>(pEngine->GetEngine())->GetIsolate(), nullptr);
        NEQUALS(reinterpret_cast<Engines::Node::Engine *>(pEngine->GetEngine())->GetPlatform(), nullptr);

        // Shutdown the engine and make sure everything went down
        EQUALS(pEngine->Shutdown(), ModuleError::MODULE_NONE);
        EQUALS(reinterpret_cast<Engines::Node::Engine *>(pEngine->GetEngine())->GetIsolate(), nullptr);

        delete pEngine;
    });
})
