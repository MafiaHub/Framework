/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "metrics/registry.h"

#include <memory>

namespace Framework::Metrics::Detail {

    struct CounterHandle {
        void *impl                                        = nullptr;
        void (*increment)(void *, std::uint64_t) noexcept = nullptr;
    };

    struct GaugeHandle {
        void *impl                           = nullptr;
        void (*set)(void *, double) noexcept = nullptr;
        void (*add)(void *, double) noexcept = nullptr;
    };

    struct HistogramHandle {
        void *impl                               = nullptr;
        void (*observe)(void *, double) noexcept = nullptr;
    };

    class Backend {
      public:
        virtual ~Backend() = default;

        virtual CounterHandle RegisterCounter(std::string_view name, std::string_view help, std::initializer_list<Label> labels)                                         = 0;
        virtual GaugeHandle RegisterGauge(std::string_view name, std::string_view help, std::initializer_list<Label> labels)                                             = 0;
        virtual HistogramHandle RegisterHistogram(std::string_view name, std::string_view help, const std::vector<double> &buckets, std::initializer_list<Label> labels) = 0;

        virtual bool HasExporter() const noexcept             = 0;
        virtual std::string_view ContentType() const noexcept = 0;
        virtual void Render(std::string &out) const           = 0;
    };

    std::unique_ptr<Backend> CreateBackend();

} // namespace Framework::Metrics::Detail
