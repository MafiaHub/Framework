/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "metrics/backend.h"

#include <charconv>
#include <ostream>
#include <stdexcept>
#include <system_error>

#include <prometheus/core.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

#include <sstream>

namespace Framework::Metrics::Detail {

    namespace {
        prometheus::labels_t ToLabels(std::initializer_list<Label> labels) {
            prometheus::labels_t result;
            for (const auto &label : labels) {
                result.emplace(std::string(label.name), std::string(label.value));
            }
            return result;
        }

        void IncrementCounter(void *impl, std::uint64_t value) noexcept {
            static_cast<prometheus::counter_t<std::uint64_t> *>(impl)->Increment(value);
        }

        void SetGauge(void *impl, double value) noexcept {
            static_cast<prometheus::gauge_t<double> *>(impl)->Set(value);
        }

        void AddGauge(void *impl, double value) noexcept {
            static_cast<prometheus::gauge_t<double> *>(impl)->Increment(value);
        }

        void ObserveHistogram(void *impl, double value) noexcept {
            static_cast<prometheus::histogram_t<double> *>(impl)->Observe(value);
        }

        class PrometheusBackend final: public Backend {
          public:
            CounterHandle RegisterCounter(std::string_view name, std::string_view help, std::initializer_list<Label> labels) override {
                auto &family = _registry.Add<prometheus::counter_t<std::uint64_t>>(std::string(name), std::string(help), {});
                auto &metric = family.Add(ToLabels(labels));
                return {&metric, IncrementCounter};
            }

            GaugeHandle RegisterGauge(std::string_view name, std::string_view help, std::initializer_list<Label> labels) override {
                auto &family = _registry.Add<prometheus::gauge_t<double>>(std::string(name), std::string(help), {});
                auto &metric = family.Add(ToLabels(labels));
                return {&metric, SetGauge, AddGauge};
            }

            HistogramHandle RegisterHistogram(std::string_view name, std::string_view help, const std::vector<double> &buckets, std::initializer_list<Label> labels) override {
                const prometheus::BucketBoundaries boundaries(buckets.begin(), buckets.end());
                auto &family = _registry.Add<prometheus::histogram_t<double>>(std::string(name), std::string(help), {});
                auto &metric = family.Add(ToLabels(labels), boundaries);
                return {&metric, ObserveHistogram};
            }

            bool HasExporter() const noexcept override {
                return true;
            }

            std::string_view ContentType() const noexcept override {
                return "text/plain; version=0.0.4";
            }

            void Render(std::string &out) const override {
                std::ostringstream stream;
                _registry.serialize(stream);
                out = stream.str();
            }

          private:
            mutable prometheus::Registry _registry;
        };
    } // namespace

    std::unique_ptr<Backend> CreateBackend() {
        return std::make_unique<PrometheusBackend>();
    }

} // namespace Framework::Metrics::Detail
