/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// This file and its CPP code were implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    class SourceLocation {
      public:
        SourceLocation(std::string &&, int line);
        SourceLocation() {};

        const std::string &GetFileName() const {
            return _fileName;
        }
        int GetLineNumber() const {
            return _lineNumber;
        }

        static SourceLocation GetCurrent(v8::Isolate *);

      private:
        std::string _fileName;
        int _lineNumber = 0;
    };
} // namespace Framework::Scripting::Helpers
