/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Framework::Metrics {

    struct Label {
        std::string_view name;
        std::string_view value;
    };

    class Counter final {
      public:
        Counter() = default;

        void Inc(std::uint64_t n = 1) noexcept;

      private:
        friend class Registry;

        using IncrementFn = void (*)(void *, std::uint64_t) noexcept;

        Counter(void *impl, IncrementFn increment) noexcept: _impl(impl), _increment(increment) {}

        void *_impl            = nullptr;
        IncrementFn _increment = nullptr;
    };

    class Gauge final {
      public:
        Gauge() = default;

        void Set(double v) noexcept;
        void Add(double v) noexcept;

      private:
        friend class Registry;

        using UpdateFn = void (*)(void *, double) noexcept;

        Gauge(void *impl, UpdateFn set, UpdateFn add) noexcept: _impl(impl), _set(set), _add(add) {}

        void *_impl   = nullptr;
        UpdateFn _set = nullptr;
        UpdateFn _add = nullptr;
    };

    class Histogram final {
      public:
        Histogram() = default;

        void Observe(double v) noexcept;

      private:
        friend class Registry;

        using ObserveFn = void (*)(void *, double) noexcept;

        Histogram(void *impl, ObserveFn observe) noexcept: _impl(impl), _observe(observe) {}

        void *_impl        = nullptr;
        ObserveFn _observe = nullptr;
    };

    class Buckets final {
      public:
        Buckets() = default;

        static Buckets Exponential(double start, double factor, int count);
        static Buckets Explicit(std::initializer_list<double> bounds);

        const std::vector<double> &Bounds() const noexcept {
            return _bounds;
        }

      private:
        std::vector<double> _bounds;
    };

    class Registry final {
      public:
        static Registry &Get();

        ~Registry();

        Registry(const Registry &)            = delete;
        Registry &operator=(const Registry &) = delete;
        Registry(Registry &&)                 = delete;
        Registry &operator=(Registry &&)      = delete;

        Counter *RegisterCounter(std::string_view name, std::string_view help, std::initializer_list<Label> labels = {});
        Gauge *RegisterGauge(std::string_view name, std::string_view help, std::initializer_list<Label> labels = {});
        Histogram *RegisterHistogram(std::string_view name, std::string_view help, Buckets buckets, std::initializer_list<Label> labels = {});

        bool HasExporter() const noexcept;
        std::string_view ContentType() const noexcept;
        void Render(std::string &out) const;

      private:
        Registry();

        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

} // namespace Framework::Metrics
