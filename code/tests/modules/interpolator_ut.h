/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/interpolator.h"

#include <iostream>

[[maybe_unused]] void PrintVector3(glm::vec3 v) {
    std::cout << "POS [" << v.x << ", " << v.y << ", " << v.z << "]" << std::endl;
}

MODULE(interpolator, {
    using namespace Framework::Utils;

    IT("snaps to destination when close to target", {
        Interpolator test;
        const auto targetPos = glm::vec3(10, 0, 0);
        auto pos             = test.GetPosition();
        pos->SetErrorContributionDelayRange(0, 1);
        pos->SetTargetValue(glm::vec3(), targetPos, 20);
        pos->SetDebugTime(20);
        EQUALS(pos->UpdateTargetValue(glm::vec3()), targetPos);
    });

    IT("meets the position half-way the target", {
        Interpolator test;
        const auto targetPos = glm::vec3(100, 0, 0);

        auto pos = test.GetPosition();
        pos->SetErrorContributionDelayRange(100, 200);
        pos->SetTargetValue(glm::vec3(), targetPos, 150);
        pos->SetDebugTime(75);
        EQUALS(pos->UpdateTargetValue(glm::vec3()), glm::vec3(31.25, 0, 0));
    });

    IT("allows for partial error compensation depending on update rate", {
        Interpolator test;
        const auto targetPos = glm::vec3(100, 0, 0);

        auto pos = test.GetPosition();
        pos->SetErrorContributionDelayRange(100, 900);
        pos->SetTargetValue(glm::vec3(), targetPos, 400);
        pos->SetDebugTime(80);
        EQUALS(pos->UpdateTargetValue(glm::vec3()), glm::vec3(10.625, 0, 0));
    });

    IT("can extrapolate position by max of 50% push", {
        Interpolator test;
        const auto targetPos = glm::vec3(100, 0, 0);

        auto pos = test.GetPosition();
        pos->SetErrorContributionDelayRange(100, 200);
        pos->SetCompensationFactor(1.5f);
        pos->SetTargetValue(glm::vec3(), targetPos, 200);
        pos->SetDebugTime(1000);
        EQUALS(pos->UpdateTargetValue(glm::vec3()), glm::vec3(150, 0, 0));
    });

    IT("can perform smooth transition between points", {
        Interpolator test;
        const auto targetPos = glm::vec3(1000, 0, 0);

        auto pos = test.GetPosition();
        pos->SetErrorContributionDelayRange(0, 100);
        pos->SetTargetValue(glm::vec3(), targetPos, 100);

        pos->SetDebugTime(25);
        const auto pos1 = pos->UpdateTargetValue(glm::vec3());
        EQUALS(pos1, glm::vec3(250, 0, 0));

        pos->SetDebugTime(50);
        const auto pos2 = pos->UpdateTargetValue(pos1);
        EQUALS(pos2, glm::vec3(500, 0, 0));

        pos->SetDebugTime(75);
        const auto pos3 = pos->UpdateTargetValue(pos2);
        EQUALS(pos3, glm::vec3(750, 0, 0));

        pos->SetDebugTime(100);
        const auto pos4 = pos->UpdateTargetValue(pos3);
        EQUALS(pos4, glm::vec3(1000, 0, 0));
    });
});
