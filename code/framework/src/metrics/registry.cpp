/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// This is the ONLY translation unit in the framework that includes the vendored
// Prometheus backend. Everything else goes through metrics/registry.h. The
// "no vendored include outside src/metrics/" rule (plan §A.2/PR1) is enforced by
// a test that greps the source tree for <prometheus/.

#include "metrics/registry.h"

// Provide the standard headers the vendored prometheus headers use but do not themselves
// include. MSVC does not pull these transitively (g++ does), so without them std::to_chars /
// std::errc / std::make_error_code are unresolved in core.h's WriteValue. Kept here, inside the
// metrics insulation boundary, so the vendored copy stays patch-free for upstream updates.
#include <charconv>
#include <ostream>
#include <stdexcept>
#include <system_error>

#include <prometheus/core.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <sstream>
#include <utility>

namespace Framework::Metrics {

    namespace {
        // Convert the wrapper's label list to the backend's ordered label map.
        prometheus::labels_t toLabels(std::initializer_list<Label> labels) {
            prometheus::labels_t out;
            for (const auto &l : labels) {
                out.emplace(std::string(l.name), std::string(l.value));
            }
            return out;
        }
    } // namespace

    // --- Handle forwards --------------------------------------------------------

    void Counter::Inc(std::uint64_t n) noexcept {
        if (!_impl) {
            return;
        }
        // The backend's Increment(uint64_t) ignores zero and (in debug) rejects
        // negatives; uint64_t can't be negative, so this is always safe.
        static_cast<prometheus::counter_t<std::uint64_t> *>(_impl)->Increment(n);
    }

    void Gauge::Set(double v) noexcept {
        if (!_impl) {
            return;
        }
        static_cast<prometheus::gauge_t<double> *>(_impl)->Set(v);
    }

    void Gauge::Add(double v) noexcept {
        if (!_impl) {
            return;
        }
        static_cast<prometheus::gauge_t<double> *>(_impl)->Increment(v);
    }

    void Histogram::Observe(double v) noexcept {
        if (!_impl) {
            return;
        }
        static_cast<prometheus::histogram_t<double> *>(_impl)->Observe(v);
    }

    // --- Buckets ----------------------------------------------------------------

    Buckets Buckets::Exponential(double start, double factor, int count) {
        Buckets b;
        if (count <= 0) {
            return b;
        }
        b._bounds.reserve(static_cast<size_t>(count));
        double v = start;
        for (int i = 0; i < count; ++i) {
            b._bounds.push_back(v);
            v *= factor;
        }
        return b;
    }

    Buckets Buckets::Explicit(std::initializer_list<double> bounds) {
        Buckets b;
        b._bounds.reserve(bounds.size());
        for (const double v : bounds) {
            b._bounds.push_back(v);
        }
        return b;
    }

    // --- Registry ---------------------------------------------------------------

    struct Registry::Impl {
        prometheus::Registry reg;

        // Own the wrapper handles so their addresses are stable for process lifetime.
        std::vector<std::unique_ptr<Counter>> counters;
        std::vector<std::unique_ptr<Gauge>> gauges;
        std::vector<std::unique_ptr<Histogram>> histograms;

    };

    Registry &Registry::Get() {
        static Registry instance;
        return instance;
    }

    Registry::Registry() : _impl(std::make_unique<Impl>()) {}
    Registry::~Registry() = default;

    Counter *Registry::RegisterCounter(std::string_view name, std::string_view help, std::initializer_list<Label> labels) {
        // Re-registering an existing name returns the same family (the backend checks
        // base labels match; we always pass empty base labels and put dimensions on
        // each child, so multiple label values coexist under one family).
        auto &fam = _impl->reg.Add<prometheus::counter_t<std::uint64_t>>(std::string(name), std::string(help), {});
        auto &metric = fam.Add(toLabels(labels));
        auto handle  = std::make_unique<Counter>(static_cast<void *>(&metric));
        _impl->counters.push_back(std::move(handle));
        return _impl->counters.back().get();
    }

    Gauge *Registry::RegisterGauge(std::string_view name, std::string_view help, std::initializer_list<Label> labels) {
        auto &fam = _impl->reg.Add<prometheus::gauge_t<double>>(std::string(name), std::string(help), {});
        auto &metric = fam.Add(toLabels(labels));
        auto handle  = std::make_unique<Gauge>(static_cast<void *>(&metric));
        _impl->gauges.push_back(std::move(handle));
        return _impl->gauges.back().get();
    }

    Histogram *Registry::RegisterHistogram(std::string_view name, std::string_view help, Buckets buckets, std::initializer_list<Label> labels) {
        const prometheus::BucketBoundaries boundaries(buckets.Bounds().begin(), buckets.Bounds().end());
        auto &fam = _impl->reg.Add<prometheus::histogram_t<double>>(std::string(name), std::string(help), {});
        auto &metric = fam.Add(toLabels(labels), boundaries);
        auto handle  = std::make_unique<Histogram>(static_cast<void *>(&metric));
        _impl->histograms.push_back(std::move(handle));
        return _impl->histograms.back().get();
    }

    void Registry::Render(std::string &out) const {
        std::ostringstream oss;
        // Backend serialize: emits # HELP / # TYPE + data lines for every family,
        // sorted by family name, locale-independent. It imbues "C" and restores.
        _impl->reg.serialize(oss);

        out = oss.str();
    }

} // namespace Framework::Metrics
