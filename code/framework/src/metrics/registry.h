/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Framework::Metrics — the framework-owned metrics surface.
//
// Framework and mod code increment counters / set gauges / observe histograms
// exclusively through this header. The backend (a vendored Prometheus client
// library) is hidden behind the wrapper: only registry.cpp includes the vendored
// headers, so the backend is swappable and the rules below stay enforceable
// regardless of what implements them. This mirrors the insulation pattern
// utils/profiler.h uses for Tracy.
//
// The registry compiles into every framework target (Framework, FrameworkClient,
// FrameworkServer) so client/mod code may record metrics anywhere. Exposition
// (/metrics) is wired only in FrameworkServer; client-side visibility is Tracy's
// job. Recording into a registry nobody scrapes is cheap and harmless.
//
// Rules (see FRAMEWORK_OBSERVABILITY_PLAN.md §A.2):
// - Registration is startup-only. Register* from a hot path is a programming
//   error (asserts in debug). Labeled families pre-create every child at
//   registration; label values come from closed sets known at compile/config
//   time — never per-connection / per-entity / per-player (§A.5 cardinality).
// - Increments/sets/observes are lock-free (atomic) and safe from any thread,
//   including FTL fibers and the webserver thread.
// - Histogram bucket layouts are fixed at registration; changing them is a
//   dashboard-breaking change.

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Metrics {

    // A single name/value label pair attached to a metric child.
    struct Label {
        std::string_view name;
        std::string_view value;
    };

    // Monotonically increasing counter. Handle is stable for process lifetime.
    class Counter final {
      public:
        Counter() = default;
        explicit Counter(void *impl) noexcept : _impl(impl) {}

        // Increment by n (default 1). No-op on a default-constructed handle.
        void Inc(std::uint64_t n = 1) noexcept;

      private:
        void *_impl = nullptr;
    };

    // Gauge: a value that can go up or down.
    class Gauge final {
      public:
        Gauge() = default;
        explicit Gauge(void *impl) noexcept : _impl(impl) {}

        void Set(double v) noexcept;
        void Add(double v) noexcept;

      private:
        void *_impl = nullptr;
    };

    // Histogram: samples observations into fixed buckets registered up front.
    class Histogram final {
      public:
        Histogram() = default;
        explicit Histogram(void *impl) noexcept : _impl(impl) {}

        void Observe(double v) noexcept;

      private:
        void *_impl = nullptr;
    };

    // Bucket layout for a histogram. Fixed at registration.
    class Buckets final {
      public:
        Buckets() = default;

        // Exponential buckets: [start, start*factor, start*factor^2, ...] with
        // `count` finite upper bounds. The implicit +Inf bucket is appended by
        // the backend; do not include it. Example: exp(0.0005, 2.0, 10) yields
        // the tick-duration buckets from the observability plan.
        static Buckets Exponential(double start, double factor, int count);

        // Arbitrary explicit upper bounds (must be strictly increasing).
        static Buckets Explicit(std::initializer_list<double> bounds);

        const std::vector<double> &Bounds() const noexcept {
            return _bounds;
        }

      private:
        std::vector<double> _bounds;
    };

    // Process-wide metrics registry. Singleton; handles returned by Register*
    // are stable for the lifetime of the process.
    class Registry final {
      public:
        static Registry &Get();

        ~Registry();

        Registry(const Registry &)            = delete;
        Registry &operator=(const Registry &) = delete;
        Registry(Registry &&)                 = delete;
        Registry &operator=(Registry &&)      = delete;

        // Register a single counter child under `name` with the given labels.
        // Re-registering the same name+labels returns a handle to the existing
        // child. Call only during module init.
        Counter *RegisterCounter(std::string_view name, std::string_view help, std::initializer_list<Label> labels = {});

        // Register a single gauge child under `name`.
        Gauge *RegisterGauge(std::string_view name, std::string_view help, std::initializer_list<Label> labels = {});

        // Register a single histogram child under `name` with the given buckets.
        Histogram *RegisterHistogram(std::string_view name, std::string_view help, Buckets buckets, std::initializer_list<Label> labels = {});

        // Render Prometheus text exposition (v0.0.4) into the caller-owned buffer.
        // Metric values are atomic snapshots; producers update gauges from their
        // owning thread rather than exposing callbacks into object internals.
        void Render(std::string &out) const;

      private:
        Registry();

        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace Framework::Metrics
