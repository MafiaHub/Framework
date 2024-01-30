/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

namespace Framework::GUI {
    class Backend {
      public:
        virtual bool Init()     = 0;
        virtual bool Shutdown() = 0;
        virtual void Update()   = 0;
    };
}; // namespace Framework::GUI
