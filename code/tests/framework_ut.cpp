/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#define UNIT_MAX_MODULES 2
#include "logging/logger.h"
#include "unit.h"

/* TEST CATEGORIES */
#include "modules/interpolator_ut.h"
// #include "modules/scripting_module_ut.h"

int main() {
    UNIT_CREATE("FrameworkTests");

    Framework::Logging::GetInstance()->PauseLogging(true);

    UNIT_MODULE(interpolator);
    // UNIT_MODULE(scripting_module);

    return UNIT_RUN();
}
