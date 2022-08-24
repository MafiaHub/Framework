/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::Scripting::Engines {
    class IResource {
      public:
        virtual bool Init() = 0;
        virtual bool Shutdown() = 0;
        virtual void Update(bool) = 0;

        virtual bool IsLoaded() = 0;
      private:
        virtual bool LoadPackageFile() = 0;
        virtual bool WatchChanges() = 0;
        virtual bool Compile(const std::string &, const std::string &) = 0;
        virtual bool Run() = 0;
    };
}