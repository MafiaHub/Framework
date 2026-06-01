/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Scripting {
    class Engine;
    class ResourceManager;

    /**
     * Base interface for scripting modules.
     *
     * Both ServerScriptingModule and ClientScriptingModule inherit from this
     * so they can be registered in CoreModules as a single ScriptingModule.
     */
    class ScriptingModule {
      public:
        virtual ~ScriptingModule() = default;

        virtual Engine *GetScriptingEngine() const = 0;
        virtual ResourceManager *GetResourceManager() const = 0;
    };
} // namespace Framework::Scripting
