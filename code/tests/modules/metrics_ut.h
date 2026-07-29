/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "jobs/job_system.h"
#include "metrics/registry.h"

#include <atomic>
#include <string>
#include <vector>

namespace {
    bool RunScheduleBatchTest(size_t batchSize) {
        Framework::Jobs::JobSystemConfig config;
        config.workerThreadCount = 2;
        config.fiberPoolSize     = 32;
        Framework::Jobs::JobSystem jobs(config);
        if (jobs.Init() != Framework::Jobs::JobSystemError::JOB_SYSTEM_NONE) {
            return false;
        }

        std::vector<int> values {1, 2, 3, 4, 5};
        std::atomic<bool> receivedScheduler = true;
        jobs.ScheduleBatch(
            values,
            [&receivedScheduler](ftl::TaskScheduler *scheduler, int *value) {
                if (!scheduler) {
                    receivedScheduler.store(false, std::memory_order_relaxed);
                }
                *value *= 2;
            },
            batchSize, ftl::TaskPriority::High);

        const std::vector<int> expected {2, 4, 6, 8, 10};
        return receivedScheduler.load(std::memory_order_relaxed) && values == expected;
    }
} // namespace

#if defined(FW_METRICS_BACKEND_PROMETHEUS)

MODULE(metrics, {
    using Framework::Metrics::Buckets;
    using Framework::Metrics::Counter;
    using Framework::Metrics::Gauge;
    using Framework::Metrics::Histogram;
    using Framework::Metrics::Registry;

    auto &reg = Registry::Get();

    IT("reports exporter capability", {
        EQUALS(reg.HasExporter(), true);
        EQUALS(reg.ContentType(), "text/plain; version=0.0.4");
    });

    IT("renders a counter with HELP/TYPE and its value", {
        Counter *c = reg.RegisterCounter("ut_exposition_counter_total", "exposition test counter");
        c->Inc(5);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("# TYPE ut_exposition_counter_total counter") != std::string::npos, true);
        EQUALS(out.find("# HELP ut_exposition_counter_total exposition test counter") != std::string::npos, true);
        EQUALS(out.find("ut_exposition_counter_total 5") != std::string::npos, true);
    });

    IT("renders a labeled counter child with its label set", {
        Counter *c = reg.RegisterCounter("ut_labeled_counter_total", "labeled test counter", {{"stage", "auth"}});
        c->Inc();
        std::string out;
        reg.Render(out);
        EQUALS(out.find("ut_labeled_counter_total{stage=\"auth\"} 1") != std::string::npos, true);
    });

    IT("re-registering the same name+labels aliases the same underlying child", {
        Counter *a = reg.RegisterCounter("ut_idempotent_counter_total", "idempotent test counter", {{"k", "v"}});
        Counter *b = reg.RegisterCounter("ut_idempotent_counter_total", "idempotent test counter", {{"k", "v"}});
        a->Inc(3);
        b->Inc(2);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("ut_idempotent_counter_total{k=\"v\"} 5") != std::string::npos, true);
    });

    IT("renders a gauge value set via Set", {
        Gauge *g = reg.RegisterGauge("ut_exposition_gauge", "exposition test gauge");
        g->Set(42.0);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("# TYPE ut_exposition_gauge gauge") != std::string::npos, true);
        EQUALS(out.find("ut_exposition_gauge 42") != std::string::npos, true);
    });

    IT("renders concurrent-style gauge increments and decrements", {
        Gauge *g = reg.RegisterGauge("ut_additive_gauge", "additive gauge test");
        g->Set(0.0);
        g->Add(3.0);
        g->Add(-1.0);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("ut_additive_gauge 2") != std::string::npos, true);
    });

    IT("renders a histogram with TYPE, +Inf bucket, count and sum", {
        Histogram *h = reg.RegisterHistogram("ut_exposition_hist_seconds", "exposition test histogram", Buckets::Exponential(0.00005, 4.0, 6));
        h->Observe(0.001);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("# TYPE ut_exposition_hist_seconds histogram") != std::string::npos, true);
        EQUALS(out.find("ut_exposition_hist_seconds_bucket{le=\"+Inf\"} 1") != std::string::npos, true);
        EQUALS(out.find("ut_exposition_hist_seconds_count 1") != std::string::npos, true);
        const std::string needle = "ut_exposition_hist_seconds_bucket{le=\"";
        size_t pos = 0, finite = 0;
        while ((pos = out.find(needle, pos)) != std::string::npos) {
            if (out.compare(pos + needle.size(), 4, "+Inf") != 0) {
                ++finite;
            }
            pos += needle.size();
        }
        EQUALS(finite, 6);
    });

    IT("keeps labeled histogram children in one family", {
        const auto buckets = Buckets::Explicit({0.001, 0.01, 0.1});
        Histogram *high    = reg.RegisterHistogram("ut_labeled_hist_seconds", "labeled histogram test", buckets, {{"priority", "high"}});
        Histogram *normal  = reg.RegisterHistogram("ut_labeled_hist_seconds", "labeled histogram test", buckets, {{"priority", "normal"}});
        high->Observe(0.002);
        normal->Observe(0.02);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("ut_labeled_hist_seconds_count{priority=\"high\"} 1") != std::string::npos, true);
        EQUALS(out.find("ut_labeled_hist_seconds_count{priority=\"normal\"} 1") != std::string::npos, true);
    });

    IT("records job queue, active, wait, and execution metrics", {
        Framework::Jobs::JobSystemConfig config;
        config.workerThreadCount = 2;
        config.fiberPoolSize     = 32;
        Framework::Jobs::JobSystem jobs(config);
        EQUALS(jobs.Init() == Framework::Jobs::JobSystemError::JOB_SYSTEM_NONE, true);

        std::atomic<bool> ran = false;
        auto waitGroup        = jobs.CreateWaitGroup();
        jobs.Schedule(
            waitGroup.get(),
            [&ran]() {
                ran.store(true, std::memory_order_relaxed);
            },
            ftl::TaskPriority::High);
        waitGroup->Wait();
        EQUALS(ran.load(std::memory_order_relaxed), true);

        std::string out;
        reg.Render(out);
        const auto metricValue = [&out](const std::string &metric) {
            const size_t metricStart = out.find(metric);
            return metricStart == std::string::npos ? -1.0 : std::stod(out.substr(metricStart + metric.size()));
        };
        EQUALS(out.find("fw_jobs_queue_depth{priority=\"high\"}") != std::string::npos, true);
        EQUALS(metricValue("fw_jobs_queue_depth{priority=\"high\"} ") >= 0.0, true);
        EQUALS(out.find("fw_jobs_active ") != std::string::npos, true);
        EQUALS(metricValue("fw_jobs_active ") >= 0.0, true);
        EQUALS(out.find("fw_jobs_queue_wait_duration_seconds_count{priority=\"high\"}") != std::string::npos, true);
        EQUALS(metricValue("fw_jobs_queue_wait_duration_seconds_count{priority=\"high\"} ") >= 1.0, true);
        EQUALS(out.find("fw_jobs_execution_duration_seconds_count{priority=\"high\"}") != std::string::npos, true);
        EQUALS(metricValue("fw_jobs_execution_duration_seconds_count{priority=\"high\"} ") >= 1.0, true);
    });

    IT("processes batched jobs", {
        EQUALS(RunScheduleBatchTest(2), true);
        EQUALS(RunScheduleBatchTest(0), true);
    });

    IT("is null-safe on a default-constructed handle", {
        Counter nullCounter;
        nullCounter.Inc();
        nullCounter.Inc(10);
        Gauge nullGauge;
        nullGauge.Set(1.0);
        nullGauge.Add(1.0);
        Histogram nullHist;
        nullHist.Observe(1.0);
    });
});

#else

MODULE(metrics, {
    using Framework::Metrics::Buckets;
    using Framework::Metrics::Registry;

    auto &reg = Registry::Get();

    IT("disables pull export", {
        EQUALS(reg.HasExporter(), false);
        EQUALS(reg.ContentType().empty(), true);
        std::string out = "stale";
        reg.Render(out);
        EQUALS(out.empty(), true);
    });

    IT("returns safe no-op handles", {
        reg.RegisterCounter("ut_none_counter_total", "none backend counter")->Inc();
        auto *gauge = reg.RegisterGauge("ut_none_gauge", "none backend gauge");
        gauge->Set(1.0);
        gauge->Add(-1.0);
        reg.RegisterHistogram("ut_none_histogram", "none backend histogram", Buckets::Explicit({1.0}))->Observe(1.0);
    });

    IT("processes batched jobs", {
        EQUALS(RunScheduleBatchTest(2), true);
        EQUALS(RunScheduleBatchTest(0), true);
    });
});

#endif
