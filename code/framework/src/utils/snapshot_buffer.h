/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <glm/ext.hpp>
#include <mafianet/get_time.h>
#include <utils/time.h>

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
        // Send intervals the adaptive delay keeps in hand: 1 renders on the newest sample just as the
        // next is due, 2 also rides out one lost unreliable packet without extrapolating.
        float delayIntervals = 1.0f;
        // Share of wall time the presentation clock may gain or lose per frame while the delay
        // adapts; doubled to fall back once it has run past the newest sample.
        float slewRate = 0.1f;
    };

    // Per-remote ring buffer of timestamped snapshots. The receiver renders a fixed delay behind
    // the newest sample and interpolates between the two bracketing snapshots, which decouples
    // render rate from network rate and absorbs jitter. On buffer underrun it extrapolates along
    // the last velocity (capped); a jump beyond a speed-scaled threshold is treated as a teleport
    // and clears history so the sampler latches the new pose instead of sweeping across the map.
    //
    // Feed it from NetworkEntity::OnDeserialized(transformUpdated) and sample every frame at
    // RenderTimeMs(GetTimeUS() / 1000.0). The presentation clock slews toward the adaptive delay
    // instead of jumping whenever a packet changes that delay, so a sender that changes rate (a
    // server rate band, say) moves the delay gradually rather than stepping the pose.
    //
    // The adaptive delay is delayIntervals x the mean send interval, plus 3x its jitter, plus how
    // late samples arrive: given the local receive time, Push tracks a decaying peak of
    // (arrival - time), so one late packet raises the delay and it relaxes slowly afterwards.
    // A gap far longer than the cadence behind a body at rest (an idle sender, whose unchanged pose
    // was not sent again, starting to move) is a restart, not a slow link: it leaves the estimate
    // alone, and the body rests in place until one interval before the new sample instead of
    // gliding across the whole pause. Behind a moving body the same gap is a stall or a slower
    // rate band: the body glides on from where it was, and the gap feeds the interval and jitter
    // estimate (clamped), so repeated stalls raise the delay and a new rate is learnt.
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

        // A gap at least this long, and at least three learnt intervals, behind a body at rest ends a
        // stream rather than slowing it.
        static constexpr float kRestartGapMs = 250.0f;
        // At rest: slower than this (units/s), or covering under kRestCoverage of what its velocity
        // would have over the gap (a body held against a wall, whose unchanged pose was not resent).
        static constexpr float kRestSpeed    = 0.1f;
        static constexpr float kRestCoverage = 0.25f;
        // The most one gap may move the interval or jitter estimate: a stall of seconds raises the
        // delay to its cap without pinning it there for seconds after.
        static constexpr float kMaxEstimateGapMs = 1000.0f;

        // `arrival` is the local clock (ms) the sample was received at, 0 when unknown.
        void Push(const TSnapshot &snapshot, MafiaNet::Time time, double arrival = 0.0) {
            if (time == 0) {
                return;
            }
            if (_count > 0) {
                const Entry prev = Newest();
                if (time <= prev.time) {
                    return;
                }

                const float gap       = static_cast<float>(time - prev.time);
                const float speed     = glm::length(TPolicy::Velocity(prev.snapshot));
                const float moved     = glm::distance(TPolicy::Position(snapshot), TPolicy::Position(prev.snapshot));
                const bool resting    = speed < kRestSpeed || moved < kRestCoverage * speed * gap * 0.001f;
                const bool longGap    = gap >= std::max(kRestartGapMs, 3.0f * _avgIntervalMs);
                if (longGap && resting) {
                    // Resumed after a pause: rest at the last pose until one interval before this one.
                    const float lead            = _avgIntervalMs > 0.0f ? _avgIntervalMs : gap * 0.5f;
                    const MafiaNet::Time restAt = time - static_cast<MafiaNet::Time>(lead);
                    if (restAt > prev.time) {
                        Append(TPolicy::Extrapolate(prev.snapshot, 0.0f), restAt);
                    }
                }
                else if (_avgIntervalMs == 0.0f) {
                    _avgIntervalMs = std::min(gap, kMaxEstimateGapMs);
                }
                else {
                    // A stall counts as at most two intervals toward the mean, so a slower band
                    // is still learnt within a few samples, while its whole length counts as jitter.
                    const float interval = longGap ? std::min(gap, 2.0f * _avgIntervalMs) : gap;
                    _jitterMs            = glm::mix(_jitterMs, std::min(std::fabs(gap - _avgIntervalMs), kMaxEstimateGapMs), 0.1f);
                    _avgIntervalMs       = glm::mix(_avgIntervalMs, std::min(interval, kMaxEstimateGapMs), 0.1f);
                }

                const float threshold = Config().warpBase + Config().warpPerSpeed * speed;
                if (glm::distance(TPolicy::Position(snapshot), TPolicy::Position(prev.snapshot)) > threshold) {
                    Clear();
                }
            }
            if (arrival > 0.0) {
                const float lateness = static_cast<float>(glm::clamp(arrival - static_cast<double>(time), 0.0, 1000.0));
                _latenessMs          = std::max(lateness, _latenessMs * 0.97f);
            }
            Append(snapshot, time);
        }

        // Returns false only when empty. When supplied, held identifies a
        // fixed pose (startup, before history, or the extrapolation cap), so
        // presentation can stop movement without changing received state.
        // renderTime is local-clock milliseconds, fractional so high frame rates move evenly.
        bool Sample(double renderTime, TSnapshot &out, bool *held = nullptr) const {
            if (held != nullptr) {
                *held = false;
            }
            if (_count == 0) {
                return false;
            }
            if (held != nullptr) {
                *held = true;
            }
            if (_count == 1) {
                out = At(0).snapshot;
                return true;
            }

            const Entry &oldest = At(0);
            const Entry &newest = Newest();

            // Behind the buffer: hold the oldest sample (better than guessing further into the past).
            if (renderTime <= static_cast<double>(oldest.time)) {
                out = oldest.snapshot;
                return true;
            }

            // Ahead of the buffer (underrun): extrapolate from the newest along its velocity, capped.
            if (renderTime >= static_cast<double>(newest.time)) {
                double dt        = Time::MsToSeconds(renderTime - static_cast<double>(newest.time));
                const double cap = Time::MsToSeconds(static_cast<double>(Config().maxExtrapolationMs));
                if (dt > cap) {
                    dt = cap;
                }
                if (held != nullptr) {
                    *held = dt >= cap;
                }
                out = TPolicy::Extrapolate(newest.snapshot, static_cast<float>(dt));
                return true;
            }

            // Interpolate between the bracketing pair.
            for (int i = 0; i < _count - 1; ++i) {
                const Entry &a = At(i);
                const Entry &b = At(i + 1);
                if (renderTime >= static_cast<double>(a.time) && renderTime <= static_cast<double>(b.time)) {
                    const float span  = static_cast<float>(b.time - a.time);
                    const float alpha = span > 0.0f ? static_cast<float>((renderTime - static_cast<double>(a.time)) / span) : 0.0f;
                    out               = TPolicy::Blend(a.snapshot, b.snapshot, alpha, span);
                    if (held != nullptr) {
                        *held = false;
                    }
                    return true;
                }
            }

            out = newest.snapshot;
            return true;
        }

        void Clear() {
            _head               = 0;
            _count              = 0;
            _avgIntervalMs      = 0.0f;
            _jitterMs           = 0.0f;
            _latenessMs         = 0.0f;
            _renderClockStarted = false;
        }

        bool Empty() const {
            return _count == 0;
        }

        float EffectiveDelayMs() const {
            if (!Config().adaptiveDelay || _avgIntervalMs == 0.0f) {
                return Config().interpDelayMs;
            }
            const float delay = Config().delayIntervals * _avgIntervalMs + 3.0f * _jitterMs + _latenessMs;
            return glm::clamp(delay, Config().minDelayMs, Config().maxDelayMs);
        }

        // Mean send interval measured so far, 0 before two samples.
        float IntervalMs() const {
            return _avgIntervalMs;
        }

        // Move the presentation clock at 90..110% of wall time while its
        // delay adapts. Directly subtracting a new delay can reverse motion
        // on a late packet, or jump forward as jitter decays. Keep fractional
        // milliseconds so high frame rates do not round away the correction.
        MafiaNet::Time RenderTime(MafiaNet::Time now) {
            return static_cast<MafiaNet::Time>(RenderTimeMs(static_cast<double>(now)));
        }

        double RenderTimeMs(double now) {
            const double target = glm::max(0.0, now - EffectiveDelayMs());
            if (!_renderClockStarted) {
                // Not while the target is clamped to zero: the clock would then advance from zero with
                // wall time and run ahead of a target still held back by the delay.
                if (target <= 0.0) {
                    return 0.0;
                }
                _renderClockStarted = true;
                _renderTimeMs       = target;
                _lastRenderNow      = now;
            }
            else if (now > _lastRenderNow) {
                const double elapsed = now - _lastRenderNow;
                _lastRenderNow       = now;
                _renderTimeMs += elapsed;
                const bool overrun = _count > 0 && _renderTimeMs > static_cast<double>(Newest().time) && target < _renderTimeMs;
                const double slew  = elapsed * static_cast<double>(Config().slewRate) * (overrun ? 2.0 : 1.0);
                _renderTimeMs += glm::clamp(target - _renderTimeMs, -slew, slew);
            }
            return _renderTimeMs;
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

        void Append(const TSnapshot &snapshot, MafiaNet::Time time) {
            _ring[_head].snapshot = snapshot;
            _ring[_head].time     = time;
            _head                 = (_head + 1) % _capacity;
            if (_count < _capacity) {
                ++_count;
            }
        }

        const SnapshotBufferConfig *_config = nullptr;

        Entry _ring[_capacity];
        int _head  = 0; // next write slot
        int _count = 0;

        float _avgIntervalMs = 0.0f;
        float _jitterMs      = 0.0f;
        float _latenessMs    = 0.0f;

        bool _renderClockStarted = false;
        double _lastRenderNow    = 0.0;
        double _renderTimeMs     = 0.0;
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
