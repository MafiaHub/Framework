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

MODULE(scripting_engine, {
    using namespace Framework::Scripting;

    IT("can allocate and deallocate a valid scripting engine instance, then do it again to test re-entry", {
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

        // Intentionally duplicated to test engine for re-entry
        pEngine = new Module;

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
