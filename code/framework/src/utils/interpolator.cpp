/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "interpolator.h"

#include <algorithm>

namespace math {
    float Unlerp(float from, float to, float pos) {
        if (std::abs(from - to) < std::numeric_limits<float>::epsilon()) {
            return 1.0f;
        }
        return (pos - from) / (to - from);
    }

    float Unlerp(const std::chrono::high_resolution_clock::time_point& from,
                 const std::chrono::high_resolution_clock::time_point& to,
                 const std::chrono::high_resolution_clock::time_point& pos) {
        const float r = std::chrono::duration<float, std::milli>(to - from).count();
        if (r < std::numeric_limits<float>::epsilon()) {
            return 1.0f;
        }
        return std::chrono::duration<float, std::milli>(pos - from).count() / r;
    }

    float UnlerpClamped(float from, float to, float pos) {
        return std::clamp(Unlerp(from, to, pos), 0.0f, 1.0f);
    }
}

namespace Framework::Utils {
    // Vec3 Policy Implementation
    glm::vec3 InterpolationPolicy<glm::vec3>::calculateError(const glm::vec3& current, const glm::vec3& target) {
        return target - current;
    }

    glm::vec3 InterpolationPolicy<glm::vec3>::scaleError(const glm::vec3& error, float factor) {
        return error * factor;
    }

    glm::vec3 InterpolationPolicy<glm::vec3>::interpolate(const glm::vec3& current, const glm::vec3& error, float alpha) {
        return current + (error * alpha);
    }

    bool InterpolationPolicy<glm::vec3>::shouldSnap(const glm::vec3& current, const glm::vec3& target) {
        return glm::distance(current, target) <= DEFAULT_SNAP_THRESHOLD;
    }

    // Quaternion Policy Implementation
    glm::quat InterpolationPolicy<glm::quat>::calculateError(const glm::quat& current, const glm::quat& target) {
        return glm::slerp(
            glm::identity<glm::quat>(),
            glm::normalize(glm::inverse(glm::normalize(current))) * glm::normalize(target),
            1.0f
        );
    }

    glm::quat InterpolationPolicy<glm::quat>::scaleError(const glm::quat& error, float factor) {
        return glm::slerp(glm::identity<glm::quat>(), error, factor);
    }

    glm::quat InterpolationPolicy<glm::quat>::interpolate(const glm::quat& current, const glm::quat& error, float alpha) {
        return glm::normalize(current * glm::slerp(glm::identity<glm::quat>(), error, alpha));
    }

    bool InterpolationPolicy<glm::quat>::shouldSnap(const glm::quat& current, const glm::quat& target) {
        return glm::dot(current, target) > 0.9999f;
    }

    // Float Policy Implementation
    float InterpolationPolicy<float>::calculateError(float current, float target) {
        return target - current;
    }

    float InterpolationPolicy<float>::scaleError(float error, float factor) {
        return error * factor;
    }

    float InterpolationPolicy<float>::interpolate(float current, float error, float alpha) {
        return current + (error * alpha);
    }

    bool InterpolationPolicy<float>::shouldSnap(float current, float target) {
        return std::abs(current - target) <= DEFAULT_SNAP_THRESHOLD;
    }

    // Interpolator Implementation
    Interpolator::Interpolator() {
        _position.setErrorContributionDelayRange(_delayMin, _delayMax);
        _rotation.setErrorContributionDelayRange(_delayMin, _delayMax);
        _scalar.setErrorContributionDelayRange(_delayMin, _delayMax);
    }

    Interpolator::Position* Interpolator::getPosition() {
        return &_position;
    }

    Interpolator::Rotation* Interpolator::getRotation() {
        return &_rotation;
    }

    Interpolator::Scalar* Interpolator::getScalar() {
        return &_scalar;
    }
}
