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
#include "modules/client_engine_ut.h"
#include "modules/dependency_graph_ut.h"
#include "modules/environment_sandbox_ut.h"
#include "modules/interpolator_ut.h"
#include "modules/resource_manager_ut.h"
#include "modules/resource_manifest_ut.h"
#include "modules/resource_ut.h"
#include "modules/scripting_module_ut.h"
#include "modules/state_machine_ut.h"

int main() {
    UNIT_CREATE("FrameworkTests");

    Framework::Logging::GetInstance()->PauseLogging(true);

    UNIT_MODULE(client_engine);
    UNIT_MODULE(dependency_graph);
    UNIT_MODULE(environment_sandbox);
    UNIT_MODULE(interpolator);
    UNIT_MODULE(resource_manager);
    UNIT_MODULE(resource_manifest);
    UNIT_MODULE(resource);
    UNIT_MODULE(scripting_module);
    UNIT_MODULE(state_machine);

    return UNIT_RUN();
}
