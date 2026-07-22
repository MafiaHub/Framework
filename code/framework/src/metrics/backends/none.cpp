/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "metrics/backend.h"

namespace Framework::Metrics::Detail {

    namespace {
        class NoneBackend final: public Backend {
          public:
            CounterHandle RegisterCounter(std::string_view, std::string_view, std::initializer_list<Label>) override {
                return {};
            }

            GaugeHandle RegisterGauge(std::string_view, std::string_view, std::initializer_list<Label>) override {
                return {};
            }

            HistogramHandle RegisterHistogram(std::string_view, std::string_view, const std::vector<double> &, std::initializer_list<Label>) override {
                return {};
            }

            bool HasExporter() const noexcept override {
                return false;
            }

            std::string_view ContentType() const noexcept override {
                return {};
            }

            void Render(std::string &out) const override {
                out.clear();
            }
        };
    } // namespace

    std::unique_ptr<Backend> CreateBackend() {
        return std::make_unique<NoneBackend>();
    }

} // namespace Framework::Metrics::Detail
