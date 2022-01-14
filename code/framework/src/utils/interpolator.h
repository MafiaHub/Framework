/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <functional>
#include <glm/ext.hpp>

namespace Framework::Utils {
    class Interpolator {
      public:
        using TimePoint = std::chrono::high_resolution_clock::time_point;
        template <typename T>
        class Value {
          public:
            explicit Value() {
                _startTime  = TimePoint::max();
                _finishTime = TimePoint::max();
            }
            virtual bool HasTargetValue() const {
                return _finishTime != TimePoint::max();
            };

            /**
             * Sets up interpolation for the current update stage.
             * @param current
             * @param target
             * @param delay Time since the last update (ideally avg / tickrate)
             */
            virtual void SetTargetValue(const T &current, const T &target, float delay) = 0;

            /**
             * Calculates the currently interpolated value and advances interpolation.
             * @param current
             * @return
             */
            virtual T UpdateTargetValue(const T &current) = 0;

            /**
             * Sets up optimal update rate range, so that if we're closer to lower bound, we calculate smaller
             * error compensation as opposed to higher bound rates.
             * @param delayMin
             * @param delayMax
             */
            void SetErrorContributionDelayRange(float delayMin, float delayMax) {
                _delayMin = delayMin;
                _delayMax = delayMax;
            }

            /**
             * Sets compensation factor to improve calculation results during over-compensation.
             * @param factor
             */
            void SetCompensationFactor(float factor) {
                _compensationFactor = factor;
            }

            /**
             * Used internally by unit tests to simulate the passage of time.
             * @param debugTime
             */
            void SetDebugTime(int64_t debugTime);

          protected:
            T _start;
            T _end;
            T _error;
            TimePoint _startTime;
            TimePoint _finishTime;
            float _lastAlpha          = 0.0f;
            float _delayMin           = 100.f;
            float _delayMax           = 400.f;
            float _compensationFactor = 1.0f;
            bool _debugEnabled        = false;
            std::chrono::milliseconds _debugTime {};

            TimePoint GetCurrentTime();
        };

        Value<glm::vec3> *GetPosition() {
            return &_position;
        }

        Value<glm::quat> *GetRotation() {
            return &_rotation;
        }

        Interpolator() {
            _position.SetErrorContributionDelayRange(_delayMin, _delayMax);
            _rotation.SetErrorContributionDelayRange(_delayMin, _delayMax);
        }

      protected:
        class Position: public Value<glm::vec3> {
          public:
            Position() = default;
            void SetTargetValue(const glm::vec3 &current, const glm::vec3 &target, float delay) override;

            glm::vec3 UpdateTargetValue(const glm::vec3 &current) override;

            /**
             * Set snap threshold when reaching target position. Used to finalize interpolation early, so that
             * we avoid calculation errors caused by marginal distance.
             * @param threshold
             */
            void SetSnapThreshold(float threshold) {
                _snapThreshold = threshold;
            }

          protected:
            float _snapThreshold = 0.001f;
        };

        class Rotation: public Value<glm::quat> {
          public:
            Rotation() = default;
            void SetTargetValue(const glm::quat &current, const glm::quat &target, float delay) override;

            glm::quat UpdateTargetValue(const glm::quat &current) override;
        };

        class Scalar: public Value<float> {
          public:
            Scalar() = default;
            void SetTargetValue(const float &current, const float &target, float delay) override;

            float UpdateTargetValue(const float &current) override;
        };

        float _delayMin = 100.f;
        float _delayMax = 400.f;

      private:
        Position _position;
        Rotation _rotation;
    };

    template <typename T>
    void Interpolator::Value<T>::SetDebugTime(int64_t debugTime) {
        _debugEnabled = true;
        _debugTime    = std::chrono::milliseconds(debugTime);
    }
    template <typename T>
    Interpolator::TimePoint Interpolator::Value<T>::GetCurrentTime() {
        if (_debugEnabled) {
            return _startTime + _debugTime;
        }
        else {
            return std::chrono::high_resolution_clock::now();
        }
    }
} // namespace Framework::Utils
