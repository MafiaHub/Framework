/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#define V8_IN_GET_WORLD()\
    reinterpret_cast<Framework::Integrations::Scripting::ServerEngine *>(resource->GetEngine())->GetWorldEngine()
