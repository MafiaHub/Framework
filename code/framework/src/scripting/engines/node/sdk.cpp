/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "sdk.h"

namespace Framework::Scripting::Engines::Node {
    bool SDK::Init(v8::Isolate *isolate) {
        _module = new v8pp::module(isolate);
        _module->const_("PI", 3.1415);
    }
} // namespace Framework::Scripting::Engines::Node