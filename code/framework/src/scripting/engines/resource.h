/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Scripting {
    class Module;
};

namespace Framework::Scripting::Engines {
    class IResource {
      public:
        virtual ~IResource() {}
        virtual bool Init()       = 0;
        virtual bool Shutdown()   = 0;
        virtual void Update(bool) = 0;
        virtual void Preload()    = 0;

        virtual bool IsLoaded() = 0;

        void SetModule(Scripting::Module *module) {
            _module = module;
        }

        Module *GetModule() {
            return _module;
        }

      private:
        virtual bool LoadPackageFile()                                 = 0;
        virtual bool WatchChanges()                                    = 0;
        virtual bool Compile(const std::string &, const std::string &) = 0;
        virtual bool Run()                                             = 0;

      protected:
        Module *_module = nullptr;
    };
} // namespace Framework::Scripting::Engines
