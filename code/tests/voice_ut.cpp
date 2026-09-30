/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "unit.h"

#include "modules/playout_buffer_ut.h"
#include "modules/spsc_ring_ut.h"
#include "modules/voice_activity_ut.h"
#include "modules/voice_mixer_ut.h"

int main() {
    UNIT_CREATE("FrameworkVoiceTests");
    UNIT_MODULE(spsc_ring);
    UNIT_MODULE(voice_activity);
    UNIT_MODULE(voice_mixer);
    UNIT_MODULE(playout_buffer);
    return UNIT_RUN();
}
