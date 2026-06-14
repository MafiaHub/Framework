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
#include "modules/type_name_ut.h"

// Scripting tests
#include "modules/engine_ut.h"
#include "modules/resource_ut.h"
#include "modules/resource_manager_ut.h"
#include "modules/js_features_ut.h"
#include "modules/timer_context_ut.h"

int main() {
    UNIT_CREATE("FrameworkTests");

    Framework::Logging::GetInstance()->PauseLogging(true);

    UNIT_MODULE(interpolator);
    UNIT_MODULE(state_machine);
    UNIT_MODULE(type_name);

    // Scripting tests
    UNIT_MODULE(engine);
    UNIT_MODULE(resource);
    UNIT_MODULE(resource_manager);
    UNIT_MODULE(js_features);
    UNIT_MODULE(timer_context);

    return UNIT_RUN();
}
