/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <string>

namespace Framework::GUI {
    class SystemClipboard final {
      public:
        void Clear();
        std::string ReadPlainText();
        void WritePlainText(const std::string &text);
    };
} // namespace Framework::GUI
