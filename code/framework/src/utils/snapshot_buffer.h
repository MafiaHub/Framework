/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cmath>
#include <glm/ext.hpp>
#include <mafianet/GetTime.h>

namespace Framework::Utils {
    // Tunables for SnapshotBuffer. One instance can be shared by many buffers (e.g. all remote
    // players) so a whole entity class is tuned at once.
    struct SnapshotBufferConfig {
        float interpDelayMs      = 100.0f; // render-behind delay when adaptiveDelay is off
        float maxExtrapolationMs = 150.0f; // cap on velocity extrapolation during buffer underrun
        float warpBase           = 4.0f;   // teleport threshold (units) at standstill...
        float warpPerSpeed       = 4.0f;   // ...plus this much per unit/s of speed
        bool adaptiveDelay       = true;   // derive the delay from measured interval + jitter
        float minDelayMs         = 50.0f;
        float maxDelayMs         = 200.0f;
    };

    // Per-remote ring buffer of timestamped snapshots. The receiver renders a fixed delay behind
    // the newest sample and interpolates between the two bracketing snapshots, which decouples
    // render rate from network rate and absorbs jitter. On buffer underrun it extrapolates along
    // the last velocity (capped); a jump beyond a speed-scaled threshold is treated as a teleport
    // and clears history so the sampler latches the new pose instead of sweeping across the map.
    //
    // Feed it from NetworkEntity::OnDeserialized(transformUpdated) and sample every frame at
    // MafiaNet::GetTime() - EffectiveDelayMs().
    //
    // TPolicy defines how snapshots blend:
    //   static glm::vec3 Position(const TSnapshot &);
    //   static glm::vec3 Velocity(const TSnapshot &);
    //   static TSnapshot Blend(const TSnapshot &a, const TSnapshot &b, float alpha, float spanMs);
    //   static TSnapshot Extrapolate(const TSnapshot &newest, float dtSeconds);
    template <typename TSnapshot, typename TPolicy>
    class SnapshotBuffer final {
      public:
        SnapshotBuffer() = default;
        // config must outlive the buffer; nullptr uses the defaults
        explicit SnapshotBuffer(const SnapshotBufferConfig *config): _config(config) {}

        void Push(const TSnapshot &snapshot, MafiaNet::Time time) {
            if (time == 0) {
                return;
            }
            if (_count > 0) {
                const Entry &prev = Newest();
                if (time <= prev.time) {
                    return;
                }

                const float gap = static_cast<float>(time - prev.time);
                if (_avgIntervalMs == 0.0f) {
                    _avgIntervalMs = gap;
                }
                else {
                    _jitterMs      = glm::mix(_jitterMs, std::fabs(gap - _avgIntervalMs), 0.1f);
                    _avgIntervalMs = glm::mix(_avgIntervalMs, gap, 0.1f);
                }

                const float speed     = glm::length(TPolicy::Velocity(prev.snapshot));
                const float threshold = Config().warpBase + Config().warpPerSpeed * speed;
                if (glm::distance(TPolicy::Position(snapshot), TPolicy::Position(prev.snapshot)) > threshold) {
                    Clear();
                }
            }

            _ring[_head].snapshot = snapshot;
            _ring[_head].time     = time;
            _head                 = (_head + 1) % _capacity;
            if (_count < _capacity) {
                ++_count;
            }
        }

        // Interpolated/extrapolated snapshot at renderTime. Returns false only when the buffer is
        // empty.
        bool Sample(MafiaNet::Time renderTime, TSnapshot &out) const {
            if (_count == 0) {
                return false;
            }
            if (_count == 1) {
                out = At(0).snapshot;
                return true;
            }

            const Entry &oldest = At(0);
            const Entry &newest = Newest();

            // Behind the buffer: hold the oldest sample (better than guessing further into the past).
            if (renderTime <= oldest.time) {
                out = oldest.snapshot;
                return true;
            }

            // Ahead of the buffer (underrun): extrapolate from the newest along its velocity, capped.
            if (renderTime >= newest.time) {
                double dt        = static_cast<double>(renderTime - newest.time) / 1000.0;
                const double cap = static_cast<double>(Config().maxExtrapolationMs) / 1000.0;
                if (dt > cap) {
                    dt = cap;
                }
                out = TPolicy::Extrapolate(newest.snapshot, static_cast<float>(dt));
                return true;
            }

            // Interpolate between the bracketing pair.
            for (int i = 0; i < _count - 1; ++i) {
                const Entry &a = At(i);
                const Entry &b = At(i + 1);
                if (renderTime >= a.time && renderTime <= b.time) {
                    const float span  = static_cast<float>(b.time - a.time);
                    const float alpha = span > 0.0f ? static_cast<float>(renderTime - a.time) / span : 0.0f;
                    out               = TPolicy::Blend(a.snapshot, b.snapshot, alpha, span);
                    return true;
                }
            }

            out = newest.snapshot;
            return true;
        }

        void Clear() {
            _head  = 0;
            _count = 0;
        }

        bool Empty() const {
            return _count == 0;
        }

        float EffectiveDelayMs() const {
            if (!Config().adaptiveDelay || _avgIntervalMs == 0.0f) {
                return Config().interpDelayMs;
            }
            const float delay = _avgIntervalMs + 3.0f * _jitterMs;
            return glm::clamp(delay, Config().minDelayMs, Config().maxDelayMs);
        }

      private:
        static constexpr int _capacity = 32;

        struct Entry {
            TSnapshot snapshot {};
            MafiaNet::Time time = 0;
        };

        const SnapshotBufferConfig &Config() const {
            static const SnapshotBufferConfig defaults {};
            return _config ? *_config : defaults;
        }

        // i in [0, _count): 0 = oldest
        const Entry &At(int i) const {
            const int idx = ((_head - _count + i) % _capacity + _capacity) % _capacity;
            return _ring[idx];
        }

        const Entry &Newest() const {
            return At(_count - 1);
        }

        const SnapshotBufferConfig *_config = nullptr;

        Entry _ring[_capacity];
        int _head  = 0; // next write slot
        int _count = 0;

        float _avgIntervalMs = 0.0f;
        float _jitterMs      = 0.0f;
    };

    // Default instantiation for the replication transform channel: the fields NetworkEntity's
    // SerializeTransform carries. A mod with no extra per-tick state gets working interpolation by
    // pushing these from OnDeserialized.
    struct TransformSnapshot {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::quat rotation = glm::identity<glm::quat>();
    };

    struct TransformSnapshotPolicy {
        static glm::vec3 Position(const TransformSnapshot &s) {
            return s.position;
        }
        static glm::vec3 Velocity(const TransformSnapshot &s) {
            return s.velocity;
        }
        static TransformSnapshot Blend(const TransformSnapshot &a, const TransformSnapshot &b, float alpha, float spanMs) {
            TransformSnapshot out = b;
            out.position          = glm::mix(a.position, b.position, alpha);
            out.rotation          = glm::slerp(a.rotation, b.rotation, alpha);
            // Derived on-screen velocity: actual displacement over the bracket.
            out.velocity = spanMs > 0.0f ? (b.position - a.position) * (1000.0f / spanMs) : b.velocity;
            return out;
        }
        static TransformSnapshot Extrapolate(const TransformSnapshot &newest, float dtSeconds) {
            TransformSnapshot out = newest;
            out.position += newest.velocity * dtSeconds;
            return out;
        }
    };

    using TransformSnapshotBuffer = SnapshotBuffer<TransformSnapshot, TransformSnapshotPolicy>;
} // namespace Framework::Utils
