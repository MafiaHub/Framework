/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "metrics/registry.h"

#include "metrics/backend.h"

#include <utility>

namespace Framework::Metrics {

    void Counter::Inc(std::uint64_t n) noexcept {
        if (_increment) {
            _increment(_impl, n);
        }
    }

    void Gauge::Set(double v) noexcept {
        if (_set) {
            _set(_impl, v);
        }
    }

    void Gauge::Add(double v) noexcept {
        if (_add) {
            _add(_impl, v);
        }
    }

    void Histogram::Observe(double v) noexcept {
        if (_observe) {
            _observe(_impl, v);
        }
    }

    Buckets Buckets::Exponential(double start, double factor, int count) {
        Buckets buckets;
        if (count <= 0) {
            return buckets;
        }

        buckets._bounds.reserve(static_cast<std::size_t>(count));
        double value = start;
        for (int i = 0; i < count; ++i) {
            buckets._bounds.push_back(value);
            value *= factor;
        }
        return buckets;
    }

    Buckets Buckets::Explicit(std::initializer_list<double> bounds) {
        Buckets buckets;
        buckets._bounds.assign(bounds.begin(), bounds.end());
        return buckets;
    }

    struct Registry::Impl {
        Impl(): backend(Detail::CreateBackend()) {}

        std::unique_ptr<Detail::Backend> backend;
        std::vector<std::unique_ptr<Counter>> counters;
        std::vector<std::unique_ptr<Gauge>> gauges;
        std::vector<std::unique_ptr<Histogram>> histograms;
    };

    Registry &Registry::Get() {
        static Registry instance;
        return instance;
    }

    Registry::Registry(): _impl(std::make_unique<Impl>()) {}

    Registry::~Registry() = default;

    Counter *Registry::RegisterCounter(std::string_view name, std::string_view help, std::initializer_list<Label> labels) {
        const auto backendHandle = _impl->backend->RegisterCounter(name, help, labels);
        auto handle              = std::unique_ptr<Counter>(new Counter(backendHandle.impl, backendHandle.increment));
        _impl->counters.push_back(std::move(handle));
        return _impl->counters.back().get();
    }

    Gauge *Registry::RegisterGauge(std::string_view name, std::string_view help, std::initializer_list<Label> labels) {
        const auto backendHandle = _impl->backend->RegisterGauge(name, help, labels);
        auto handle              = std::unique_ptr<Gauge>(new Gauge(backendHandle.impl, backendHandle.set, backendHandle.add));
        _impl->gauges.push_back(std::move(handle));
        return _impl->gauges.back().get();
    }

    Histogram *Registry::RegisterHistogram(std::string_view name, std::string_view help, Buckets buckets, std::initializer_list<Label> labels) {
        const auto backendHandle = _impl->backend->RegisterHistogram(name, help, buckets.Bounds(), labels);
        auto handle              = std::unique_ptr<Histogram>(new Histogram(backendHandle.impl, backendHandle.observe));
        _impl->histograms.push_back(std::move(handle));
        return _impl->histograms.back().get();
    }

    bool Registry::HasExporter() const noexcept {
        return _impl->backend->HasExporter();
    }

    std::string_view Registry::ContentType() const noexcept {
        return _impl->backend->ContentType();
    }

    void Registry::Render(std::string &out) const {
        _impl->backend->Render(out);
    }

} // namespace Framework::Metrics
