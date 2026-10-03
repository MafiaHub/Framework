/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// Dependency-free input tests: no renderer, browser, network or game process.
// clang-format off
#include "unit.h"
#include "modules/input_state_ut.h"
#ifdef _WIN32
#include "modules/physical_keys_ut.h"
#include "modules/key_labels_ut.h"
#include "modules/window_input_ut.h"
#include "modules/polling_input_ut.h"
#endif

// clang-format on

int main() {
    UNIT_CREATE("FrameworkInputTests");
    UNIT_MODULE(input_state);
#ifdef _WIN32
    UNIT_MODULE(window_input);
    UNIT_MODULE(physical_keys);
    UNIT_MODULE(key_labels);
    UNIT_MODULE(polling_input);
#endif
    return UNIT_RUN();
}
