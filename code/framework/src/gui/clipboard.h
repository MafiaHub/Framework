/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once
#include <Ultralight/Ultralight.h>

#include <Ultralight/platform/Clipboard.h>

namespace Framework::GUI {
    class SystemClipboard final: public ultralight::Clipboard {
      public:
        void Clear() override;
        ultralight::String ReadPlainText() override;
        void WritePlainText(const ultralight::String &text) override;
    };
} // namespace Framework::GUI
