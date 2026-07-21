/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// Tests for the Framework::Metrics. These exercise the registry surface
// directly — counters, gauges, histograms, label handling, and the Prometheus
// text exposition format — using ut_-prefixed names so they don't collide with metrics other test
// modules register into the same process-wide singleton.
//
// The Metrics registry is a process-wide singleton shared by every test module, so a test that
// asserted on the *whole* render output or a total series count would be order-dependent and
// flaky. Each case instead asserts on substrings for its own ut_ metrics only.

#include "jobs/job_system.h"
#include "metrics/registry.h"

#include <atomic>
#include <string>

MODULE(metrics, {
    using Framework::Metrics::Buckets;
    using Framework::Metrics::Counter;
    using Framework::Metrics::Gauge;
    using Framework::Metrics::Histogram;
    using Framework::Metrics::Registry;

    auto &reg = Registry::Get();

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
        // The backend dedups a family child by labels, so both handles address the same counter:
        // increments through either are visible through the other. This is the idempotent reuse the
        // networking/replication code relies on (the {stage="..."} family is registered from multiple TUs).
        Counter *a = reg.RegisterCounter("ut_idempotent_counter_total", "idempotent test counter", {{"k", "v"}});
        Counter *b = reg.RegisterCounter("ut_idempotent_counter_total", "idempotent test counter", {{"k", "v"}});
        a->Inc(3);
        b->Inc(2);
        std::string out;
        reg.Render(out);
        // 3 + 2 on the same child.
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
        // exp(50µs, 4×, 6) -> 6 finite buckets + the implicit +Inf bucket (plan §A.4).
        Histogram *h = reg.RegisterHistogram("ut_exposition_hist_seconds", "exposition test histogram", Buckets::Exponential(0.00005, 4.0, 6));
        h->Observe(0.001);
        std::string out;
        reg.Render(out);
        EQUALS(out.find("# TYPE ut_exposition_hist_seconds histogram") != std::string::npos, true);
        // The implicit +Inf bucket always receives every observation.
        EQUALS(out.find("ut_exposition_hist_seconds_bucket{le=\"+Inf\"} 1") != std::string::npos, true);
        // One observation -> count 1.
        EQUALS(out.find("ut_exposition_hist_seconds_count 1") != std::string::npos, true);
        // Six finite buckets present (le != +Inf). Count the bucket lines that aren't +Inf.
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
        EQUALS(out.find("fw_jobs_queue_depth{priority=\"high\"} 0") != std::string::npos, true);
        EQUALS(out.find("fw_jobs_active 0") != std::string::npos, true);
        EQUALS(out.find("fw_jobs_queue_wait_duration_seconds_count{priority=\"high\"} 1") != std::string::npos, true);
        EQUALS(out.find("fw_jobs_execution_duration_seconds_count{priority=\"high\"} 1") != std::string::npos, true);
    });

    IT("is null-safe on a default-constructed handle", {
        // A nullptr handle (e.g. metrics whose registration was skipped) must not crash.
        Counter nullCounter;
        nullCounter.Inc();
        nullCounter.Inc(10);
        Gauge nullGauge;
        nullGauge.Set(1.0);
        nullGauge.Add(1.0);
        Histogram nullHist;
        nullHist.Observe(1.0);
        // Rendering still succeeds with no additions from null handles.
        std::string out;
        reg.Render(out);
        EQUALS(out.empty(), false);
    });
});
