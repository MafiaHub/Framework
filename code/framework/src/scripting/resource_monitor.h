/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Framework::Scripting {

    // A runtime's heap and its collector's running totals, read once a second.
    struct RuntimeSample {
        uint64_t heapUsedBytes  = 0;
        uint64_t heapTotalBytes = 0;
        uint64_t heapLimitBytes = 0;
        uint64_t externalBytes  = 0;
        uint64_t gcPauses       = 0;
        double gcPauseMs        = 0.0;
    };

    // What one runtime costs. Script time is its own: time spent in runtimes it called into is theirs.
    struct RuntimeStats {
        std::string name;
        double scriptMsPerTick  = 0.0; // mean over the window
        double scriptMsMaxTick  = 0.0; // worst server tick in the window
        double scriptSeconds    = 0.0; // since the runtime started
        double gcMsPerSecond    = 0.0; // mean over the window
        uint64_t gcPauses       = 0;   // since the runtime started
        uint64_t heapUsedBytes  = 0;   // last sample
        uint64_t heapTotalBytes = 0;
        uint64_t heapLimitBytes = 0;
        uint64_t externalBytes  = 0;
        uint64_t slowReports    = 0; // handlers, listeners, calls and ticks over the threshold, since the runtime started
    };

    /**
     * Per-resource monitor (docs/scripting_resource_isolation.md, "Monitoring resources"): script time,
     * heap and collector cost for every runtime, and a warning when one handler, listener, export call or
     * runtime tick spends too long in its own script.
     *
     * Time is charged where the engine enters a runtime: its own tick, a cross-runtime call or
     * settlement, an event or message handler, a state bag listener. Entering a runtime pauses the one it
     * was entered from, so a resource is not charged for the resources it calls. Main thread only.
     */
    class ResourceMonitor final {
      public:
        using Clock = std::chrono::steady_clock;

        static constexpr size_t kWindowSeconds  = 60;
        static constexpr double kDefaultSlowMs  = 50.0;
        static constexpr auto kSlowReportPeriod = std::chrono::seconds(10);

        enum class SlowOutcome : uint8_t {
            Under,      // under the threshold, warnings off, or an untracked runtime
            Logged,     // over it, and logged
            Suppressed, // over it, but the same report was logged less than kSlowReportPeriod ago
        };

        ResourceMonitor();
        ~ResourceMonitor();

        ResourceMonitor(const ResourceMonitor &)            = delete;
        ResourceMonitor &operator=(const ResourceMonitor &) = delete;

        // `key` is what Enter() names the runtime by: its isolate.
        void Track(const void *key, std::string name);
        void Untrack(const void *key);
        bool IsTracked(const void *key) const;

        // Charge time to `key`'s runtime until the matching Leave(). An untracked key charges nobody but still
        // pauses the runtime it was entered from.
        void Enter(const void *key, Clock::time_point now);
        void Leave(Clock::time_point now);

        // Enter() and Leave() on Current(), if there is one.
        class Scope final {
          public:
            explicit Scope(const void *key);
            ~Scope();

            Scope(const Scope &)            = delete;
            Scope &operator=(const Scope &) = delete;

          private:
            ResourceMonitor *_monitor;
        };

        // All the time charged to `key`'s runtime so far, its open frame included. Read before and after a call, it
        // gives the runtime's own time in that call. 0 for an untracked key.
        double ChargedMs(const void *key, Clock::time_point now);

        // Whether a second has passed since the last one closed, so the caller should Record() every runtime.
        bool SampleDue(Clock::time_point now) const;
        void Record(const void *key, const RuntimeSample &sample);

        // Close the server tick that ends at `now`; once a second this also closes a window slot.
        void EndTick(Clock::time_point now);

        // Log `[resource] <what> '<detail>' took N ms` when `ms` is over the threshold, at most once per runtime,
        // what and detail every kSlowReportPeriod.
        SlowOutcome ReportIfSlow(const void *key, std::string_view what, std::string_view detail, double ms, Clock::time_point now);

        // ReportIfSlow() with `key`'s own time since ChargedMs() returned `chargedBefore`.
        SlowOutcome ReportOwnTime(const void *key, std::string_view what, std::string_view detail, double chargedBefore, Clock::time_point now);

        // 0 turns the warnings off.
        void SetSlowThresholdMs(double ms);
        double GetSlowThresholdMs() const {
            return _slowThresholdMs;
        }

        std::vector<RuntimeStats> Snapshot() const;

        // The installed monitor, on the thread that installed it (the server's main thread); null on any other thread
        // and outside a server engine. One per process: the engine initialised last installs its own.
        static ResourceMonitor *Current();
        static void SetCurrent(ResourceMonitor *monitor);

      private:
        struct Second {
            double scriptMs  = 0.0;
            double maxTickMs = 0.0;
            uint32_t ticks   = 0;
            double gcMs      = 0.0;
            bool gcMeasured  = false; // closed with a collector figure, not as the slot that set the baseline
        };

        struct Slot {
            std::string name;
            double tickMs  = 0.0; // charged since the last EndTick
            double totalMs = 0.0;
            std::array<Second, kWindowSeconds> window {};
            RuntimeSample sample;          // last Record()
            double gcPauseMsAtRoll = -1.0; // sample.gcPauseMs when the last slot closed; negative before the first
            uint64_t slowReports   = 0;
        };

        struct Frame {
            Slot *slot = nullptr;
            Clock::time_point since;
        };

        struct ReportState {
            Clock::time_point lastLogged;
            uint64_t suppressed = 0;
        };

        void Charge(Frame &frame, Clock::time_point now);
        void CloseSecond();

        std::unordered_map<const void *, std::unique_ptr<Slot>> _slots;
        std::vector<Frame> _stack;
        std::unordered_map<std::string, ReportState> _slowReports;
        size_t _current = 0; // the window slot this second fills
        size_t _filled  = 1; // slots holding data, the current one included
        Clock::time_point _secondStart;
        double _slowThresholdMs = kDefaultSlowMs;
    };

} // namespace Framework::Scripting
