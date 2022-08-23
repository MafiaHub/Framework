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

#include "v8_source_location.h"

#include <v8.h>

namespace Framework::Scripting::Helpers {
    struct EventCallback {
        v8::UniquePersistent<v8::Function> _fn;
        SourceLocation _location;

        bool _removed = false;
        bool _once    = false;

        EventCallback(v8::Isolate *isolate, v8::Local<v8::Function> fn, SourceLocation &&location, bool once = false)
            : _fn(isolate, fn)
            , _location(std::move(location))
            , _once(once) {}
    };
} // namespace Framework::Scripting::Helpers
