/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "interpolator.h"

#include <algorithm>
#include <glm/trigonometric.hpp>

namespace math {
    inline const float Unlerp(const float from, const float to, const float pos) {
        // Avoid dividing by 0 (results in INF values)
        if (from == to)
            return 1.0f;

        return static_cast<float>((pos - from) / (to - from));
    }

    inline const float Unlerp(const std::chrono::high_resolution_clock::time_point &from, const std::chrono::high_resolution_clock::time_point &to,
        const std::chrono::high_resolution_clock::time_point &pos) {
        float r = std::chrono::duration<float, std::milli>(to - from).count();

        // Avoid dividing by 0 (results in INF values)
        if (r < std::numeric_limits<float>::epsilon())
            return 1.0f;

        return std::chrono::duration<float, std::milli>(pos - from).count() / r;
    }

    inline const float UnlerpClamped(const float from, const float to, const float pos) {
        return std::clamp(Unlerp(from, to, pos), 0.0f, 1.0f);
    }
} // namespace math

void Framework::Utils::Interpolator::Position::SetTargetValue(const glm::vec3 &current, const glm::vec3 &target, float delay) {
    UpdateTargetValue(current);

    _end   = target;
    _start = current;
    _error = target - current;

    _error *= glm::mix(0.25f, 1.0f, math::UnlerpClamped(_delayMin, _delayMax, delay));

    _startTime  = std::chrono::high_resolution_clock::now();
    _finishTime = _startTime + std::chrono::milliseconds(static_cast<int>(delay));

    _lastAlpha = 0.0f;
}
glm::vec3 Framework::Utils::Interpolator::Position::UpdateTargetValue(const glm::vec3 &current) {
    if (!HasTargetValue())
        return current;

    const auto currentTime = GetCurrentTime();
    float alpha            = math::Unlerp(_startTime, _finishTime, currentTime);

    // NOTE: don't let it overcompensate
    alpha = std::clamp(alpha, 0.0f, _compensationFactor);

    auto currentAlpha = alpha - _lastAlpha;
    _lastAlpha        = alpha;

    glm::vec3 compensation = glm::mix(glm::vec3(), _error, currentAlpha);

    if (alpha == _compensationFactor)
        _finishTime = TimePoint::max();

    auto newPos = current + compensation;

    // NOTE: snap value to target pos if below threshold (useful when entity stands, no need to run calculations during that time)
    if (glm::distance(newPos, _end) <= _snapThreshold) {
        _finishTime = TimePoint::max();
        newPos      = _end;
    }

    return newPos;
}
void Framework::Utils::Interpolator::Rotation::SetTargetValue(const glm::quat &current, const glm::quat &target, float delay) {
    UpdateTargetValue(current);

    _end   = glm::normalize(target);
    _start = glm::normalize(current);
    _error = glm::slerp(glm::identity<glm::quat>(), glm::normalize(glm::inverse(_start)) * _end, glm::mix(0.40f, 1.0f, math::UnlerpClamped(_delayMin, _delayMax, delay)));

    _startTime  = GetCurrentTime();
    _finishTime = _startTime + std::chrono::milliseconds(static_cast<int>(delay));

    _lastAlpha = 0.0f;
}
glm::quat Framework::Utils::Interpolator::Rotation::UpdateTargetValue(const glm::quat &current) {
    if (!HasTargetValue())
        return current;

    const auto currentTime = GetCurrentTime();
    float alpha            = math::Unlerp(_startTime, _finishTime, currentTime);

    // NOTE: don't let it overcompensate
    alpha = std::clamp(alpha, 0.0f, _compensationFactor);

    auto currentAlpha = alpha - _lastAlpha;
    _lastAlpha        = alpha;

    auto compensation = glm::slerp(glm::identity<glm::quat>(), _error, currentAlpha);

    if (alpha == _compensationFactor)
        _finishTime = TimePoint::max();

    return glm::normalize(glm::normalize(current) * compensation);
}
void Framework::Utils::Interpolator::Scalar::SetTargetValue(const float &current, const float &target, float delay) {
    UpdateTargetValue(current);

    _end   = target;
    _start = current;
    _error = target - current;

    _error *= glm::mix(0.25f, 1.0f, math::UnlerpClamped(_delayMin, _delayMax, delay));

    _startTime  = std::chrono::high_resolution_clock::now();
    _finishTime = _startTime + std::chrono::milliseconds(static_cast<int>(delay));

    _lastAlpha = 0.0f;
}
float Framework::Utils::Interpolator::Scalar::UpdateTargetValue(const float &current) {
    if (!HasTargetValue())
        return current;

    const auto currentTime = GetCurrentTime();
    float alpha            = math::Unlerp(_startTime, _finishTime, currentTime);

    // NOTE: don't let it overcompensate
    alpha = std::clamp(alpha, 0.0f, _compensationFactor);

    auto currentAlpha = alpha - _lastAlpha;
    _lastAlpha        = alpha;

    float compensation = glm::mix(0.0f, _error, currentAlpha);

    if (alpha == _compensationFactor)
        _finishTime = TimePoint::max();

    return current + compensation;
}
