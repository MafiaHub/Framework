/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <optional>
#include <glm/ext.hpp>
#include <cassert>

namespace math {
    float Unlerp(float from, float to, float pos);
    float Unlerp(const std::chrono::high_resolution_clock::time_point& from,
                 const std::chrono::high_resolution_clock::time_point& to,
                 const std::chrono::high_resolution_clock::time_point& pos);
    float UnlerpClamped(float from, float to, float pos);
} // namespace math

namespace Framework::Utils {
    // Forward declarations
    template<typename T>
    class InterpolationPolicy;

    template<typename T, typename Policy = InterpolationPolicy<T>>
    class ValueInterpolator {
    public:
        using TimePoint = std::chrono::high_resolution_clock::time_point;
        
        ValueInterpolator(): 
            _startTime(TimePoint::max()),
            _finishTime(TimePoint::max()) {}

        bool hasTargetValue() const {
            return _finishTime != TimePoint::max();
        }

        virtual void setTargetValue(const T& current, const T& target, float delay) {
            assert(delay >= 0.0f && "Delay must be non-negative");
            
            updateTargetValue(current);

            _end = target;
            _start = current;
            _error = Policy::calculateError(current, target);

            float contributionFactor = glm::mix(0.25f, 1.0f, 
                math::UnlerpClamped(_delayMin, _delayMax, delay));
            _error = Policy::scaleError(_error, contributionFactor);

            _startTime = getCurrentTime();
            _finishTime = _startTime + std::chrono::milliseconds(static_cast<int>(delay));
            _lastAlpha = 0.0f;
        }

        virtual T updateTargetValue(const T& current) {
            if (!hasTargetValue()) {
                return current;
            }

            const auto currentTime = getCurrentTime();
            float alpha = math::Unlerp(_startTime, _finishTime, currentTime);
            alpha = std::clamp(alpha, 0.0f, _compensationFactor);

            const auto currentAlpha = alpha - _lastAlpha;
            _lastAlpha = alpha;

            T newValue = Policy::interpolate(current, _error, currentAlpha);

            if (alpha == _compensationFactor) {
                _finishTime = TimePoint::max();
            }

            if (Policy::shouldSnap(newValue, _end)) {
                _finishTime = TimePoint::max();
                return _end;
            }

            return newValue;
        }

        void setErrorContributionDelayRange(float delayMin, float delayMax) {
            assert(delayMin <= delayMax && "Min delay must be less than or equal to max delay");
            _delayMin = delayMin;
            _delayMax = delayMax;
        }

        void setCompensationFactor(float factor) {
            assert(factor > 0.0f && "Compensation factor must be positive");
            _compensationFactor = factor;
        }

        void setDebugTime(int64_t debugTime) {
            _debugEnabled = true;
            _debugTime = std::chrono::milliseconds(debugTime);
        }

    protected:
        TimePoint getCurrentTime() const {
            return _debugEnabled ? 
                _startTime + _debugTime : 
                std::chrono::high_resolution_clock::now();
        }

        T _start{};
        T _end{};
        T _error{};
        TimePoint _startTime;
        TimePoint _finishTime;
        float _lastAlpha = 0.0f;
        float _delayMin = 100.f;
        float _delayMax = 400.f;
        float _compensationFactor = 1.0f;
        bool _debugEnabled = false;
        std::chrono::milliseconds _debugTime{};
    };

    // Specializations declarations
    template<>
    class InterpolationPolicy<glm::vec3> {
    public:
        static constexpr float DEFAULT_SNAP_THRESHOLD = 0.001f;
        
        static glm::vec3 calculateError(const glm::vec3& current, const glm::vec3& target);
        static glm::vec3 scaleError(const glm::vec3& error, float factor);
        static glm::vec3 interpolate(const glm::vec3& current, const glm::vec3& error, float alpha);
        static bool shouldSnap(const glm::vec3& current, const glm::vec3& target);
    };

    template<>
    class InterpolationPolicy<glm::quat> {
    public:
        static glm::quat calculateError(const glm::quat& current, const glm::quat& target);
        static glm::quat scaleError(const glm::quat& error, float factor);
        static glm::quat interpolate(const glm::quat& current, const glm::quat& error, float alpha);
        static bool shouldSnap(const glm::quat& current, const glm::quat& target);
    };

    template<>
    class InterpolationPolicy<float> {
    public:
        static constexpr float DEFAULT_SNAP_THRESHOLD = 0.001f;
        
        static float calculateError(float current, float target);
        static float scaleError(float error, float factor);
        static float interpolate(float current, float error, float alpha);
        static bool shouldSnap(float current, float target);
    };

    class Interpolator final {
    public:
        using Position = ValueInterpolator<glm::vec3>;
        using Rotation = ValueInterpolator<glm::quat>;
        using Scalar = ValueInterpolator<float>;

        Interpolator();
        Position* getPosition();
        Rotation* getRotation();
        Scalar* getScalar();

    private:
        float _delayMin = 100.f;
        float _delayMax = 400.f;

        Position _position;
        Rotation _rotation;
        Scalar _scalar;
    };

} // namespace Framework::Utils