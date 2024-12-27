/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/interpolator.h"
#include <iostream>

void PrintVector3(const glm::vec3& v) {
    std::cout << "POS [" << v.x << ", " << v.y << ", " << v.z << "]" << std::endl;
}

MODULE(interpolator, {
    using namespace Framework::Utils;

    IT("snaps to destination when close to target", {
        Interpolator test;
        const auto targetPos = glm::vec3(10, 0, 0);
        auto pos = test.getPosition();
        pos->setErrorContributionDelayRange(0, 1);
        pos->setTargetValue(glm::vec3(), targetPos, 20);
        pos->setDebugTime(20);
        EQUALS(pos->updateTargetValue(glm::vec3()), targetPos);
    });

    IT("meets the position half-way the target", {
        Interpolator test;
        const auto targetPos = glm::vec3(100, 0, 0);

        auto pos = test.getPosition();
        pos->setErrorContributionDelayRange(100, 200);
        pos->setTargetValue(glm::vec3(), targetPos, 150);
        pos->setDebugTime(75);
        EQUALS(pos->updateTargetValue(glm::vec3()), glm::vec3(31.25, 0, 0));
    });

    IT("allows for partial error compensation depending on update rate", {
        Interpolator test;
        const auto targetPos = glm::vec3(100, 0, 0);

        auto pos = test.getPosition();
        pos->setErrorContributionDelayRange(100, 900);
        pos->setTargetValue(glm::vec3(), targetPos, 400);
        pos->setDebugTime(80);
        EQUALS(pos->updateTargetValue(glm::vec3()), glm::vec3(10.625, 0, 0));
    });

    IT("can extrapolate position by max of 50% push", {
        Interpolator test;
        const auto targetPos = glm::vec3(100, 0, 0);

        auto pos = test.getPosition();
        pos->setErrorContributionDelayRange(100, 200);
        pos->setCompensationFactor(1.5f);
        pos->setTargetValue(glm::vec3(), targetPos, 200);
        pos->setDebugTime(1000);
        EQUALS(pos->updateTargetValue(glm::vec3()), glm::vec3(150, 0, 0));
    });

    IT("can perform smooth transition between points", {
        Interpolator test;
        const auto targetPos = glm::vec3(1000, 0, 0);

        auto pos = test.getPosition();
        pos->setErrorContributionDelayRange(0, 100);
        pos->setTargetValue(glm::vec3(), targetPos, 100);

        pos->setDebugTime(25);
        const auto pos1 = pos->updateTargetValue(glm::vec3());
        EQUALS(pos1, glm::vec3(250, 0, 0));

        pos->setDebugTime(50);
        const auto pos2 = pos->updateTargetValue(pos1);
        EQUALS(pos2, glm::vec3(500, 0, 0));

        pos->setDebugTime(75);
        const auto pos3 = pos->updateTargetValue(pos2);
        EQUALS(pos3, glm::vec3(750, 0, 0));

        pos->setDebugTime(100);
        const auto pos4 = pos->updateTargetValue(pos3);
        EQUALS(pos4, glm::vec3(1000, 0, 0));
    });
});