/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#define UNIT_MAX_MODULES 10
#include "logging/logger.h"
#include "unit.h"

/* TEST CATEGORIES */
#include "modules/interpolator_ut.h"
#include "modules/state_machine_ut.h"

// JavaScript scripting tests
#include "modules/js_engine_ut.h"
#include "modules/js_resource_ut.h"
#include "modules/js_resource_manager_ut.h"

int main() {
    UNIT_CREATE("FrameworkTests");

    Framework::Logging::GetInstance()->PauseLogging(true);

    UNIT_MODULE(interpolator);
    UNIT_MODULE(state_machine);

    // JavaScript scripting tests
    UNIT_MODULE(js_engine);
    UNIT_MODULE(js_resource);
    UNIT_MODULE(js_resource_manager);

    return UNIT_RUN();
}
