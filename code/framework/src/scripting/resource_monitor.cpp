/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "resource_monitor.h"

#include <logging/logger.h>

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <thread>

namespace Framework::Scripting {
    namespace {
        std::atomic<ResourceMonitor *> gCurrent {nullptr};
        std::atomic<std::thread::id> gCurrentThread {};

        // Distinct resource, what and detail triples kept for rate limiting; past this the table starts over.
        constexpr size_t kMaxSlowReportKeys = 4096;

        double ToMs(ResourceMonitor::Clock::duration duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        }
    } // namespace

    ResourceMonitor::ResourceMonitor(): _secondStart(Clock::now()) {}

    ResourceMonitor::~ResourceMonitor() {
        ResourceMonitor *self = this;
        gCurrent.compare_exchange_strong(self, nullptr);
    }

    ResourceMonitor *ResourceMonitor::Current() {
        // State bag changes can be raised off the main thread; the stack is the main thread's alone.
        ResourceMonitor *monitor = gCurrent.load(std::memory_order_acquire);
        return monitor != nullptr && gCurrentThread.load(std::memory_order_relaxed) == std::this_thread::get_id() ? monitor : nullptr;
    }

    void ResourceMonitor::SetCurrent(ResourceMonitor *monitor) {
        gCurrentThread.store(std::this_thread::get_id(), std::memory_order_relaxed);
        gCurrent.store(monitor, std::memory_order_release);
    }

    void ResourceMonitor::Track(const void *key, std::string name) {
        Untrack(key);
        auto slot   = std::make_unique<Slot>();
        slot->name  = std::move(name);
        _slots[key] = std::move(slot);
    }

    void ResourceMonitor::Untrack(const void *key) {
        const auto it = _slots.find(key);
        if (it == _slots.end()) {
            return;
        }
        // A frame still open on it keeps pausing its caller, but charges nobody.
        for (auto &frame : _stack) {
            if (frame.slot == it->second.get()) {
                frame.slot = nullptr;
            }
        }
        _slots.erase(it);
    }

    bool ResourceMonitor::IsTracked(const void *key) const {
        return _slots.contains(key);
    }

    void ResourceMonitor::Charge(Frame &frame, Clock::time_point now) {
        if (frame.slot && now > frame.since) {
            const double ms = ToMs(now - frame.since);
            frame.slot->tickMs += ms;
            frame.slot->totalMs += ms;
        }
        frame.since = now;
    }

    void ResourceMonitor::Enter(const void *key, Clock::time_point now) {
        if (!_stack.empty()) {
            Charge(_stack.back(), now);
        }
        const auto it = _slots.find(key);
        _stack.push_back({it == _slots.end() ? nullptr : it->second.get(), now});
    }

    void ResourceMonitor::Leave(Clock::time_point now) {
        if (_stack.empty()) {
            return;
        }
        Charge(_stack.back(), now);
        _stack.pop_back();
        if (!_stack.empty()) {
            _stack.back().since = now;
        }
    }

    ResourceMonitor::Scope::Scope(const void *key): _monitor(Current()) {
        if (_monitor) {
            _monitor->Enter(key, Clock::now());
        }
    }

    ResourceMonitor::Scope::~Scope() {
        if (_monitor) {
            _monitor->Leave(Clock::now());
        }
    }

    double ResourceMonitor::ChargedMs(const void *key, Clock::time_point now) {
        // The open frame's time so far, so a read taken inside it agrees with one taken after it closes.
        if (!_stack.empty()) {
            Charge(_stack.back(), now);
        }
        const auto it = _slots.find(key);
        return it != _slots.end() ? it->second->totalMs : 0.0;
    }

    bool ResourceMonitor::SampleDue(Clock::time_point now) const {
        return now - _secondStart >= std::chrono::seconds(1);
    }

    void ResourceMonitor::Record(const void *key, const RuntimeSample &sample) {
        if (const auto it = _slots.find(key); it != _slots.end()) {
            it->second->sample = sample;
        }
    }

    void ResourceMonitor::EndTick(Clock::time_point now) {
        // A runtime still executing further up the stack: what it has run so far belongs to this tick.
        if (!_stack.empty()) {
            Charge(_stack.back(), now);
        }
        for (auto &[key, slot] : _slots) {
            Second &second = slot->window[_current];
            second.scriptMs += slot->tickMs;
            second.maxTickMs = std::max(second.maxTickMs, slot->tickMs);
            ++second.ticks;
            slot->tickMs = 0.0;
        }
        if (SampleDue(now)) {
            CloseSecond();
            _secondStart = now;
        }
    }

    void ResourceMonitor::CloseSecond() {
        for (auto &[key, slot] : _slots) {
            // The collector's running total, as last recorded, against the one the previous slot closed on.
            if (slot->gcPauseMsAtRoll >= 0.0) {
                slot->window[_current].gcMs       = std::max(0.0, slot->sample.gcPauseMs - slot->gcPauseMsAtRoll);
                slot->window[_current].gcMeasured = true;
            }
            slot->gcPauseMsAtRoll = slot->sample.gcPauseMs;
        }
        _current = (_current + 1) % kWindowSeconds;
        _filled  = std::min(_filled + 1, kWindowSeconds);
        for (auto &[key, slot] : _slots) {
            slot->window[_current] = {};
        }
    }

    ResourceMonitor::SlowOutcome ResourceMonitor::ReportIfSlow(const void *key, std::string_view what, std::string_view detail, double ms, Clock::time_point now) {
        if (_slowThresholdMs <= 0.0 || ms < _slowThresholdMs) {
            return SlowOutcome::Under;
        }
        const auto slotIt = _slots.find(key);
        if (slotIt == _slots.end()) {
            return SlowOutcome::Under;
        }
        Slot &slot = *slotIt->second;
        ++slot.slowReports;

        if (_slowReports.size() >= kMaxSlowReportKeys) {
            _slowReports.clear();
        }
        ReportState &report = _slowReports[fmt::format("{}\n{}\n{}", slot.name, what, detail)];
        if (report.lastLogged != Clock::time_point {} && now - report.lastLogged < kSlowReportPeriod) {
            ++report.suppressed;
            return SlowOutcome::Suppressed;
        }

        const std::string subject = detail.empty() ? std::string(what) : fmt::format("{} '{}'", what, detail);
        if (report.suppressed > 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] {} took {:.0f} ms ({} more over {:.0f} ms since the last report)", slot.name, subject, ms, report.suppressed, _slowThresholdMs);
        }
        else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("[{}] {} took {:.0f} ms", slot.name, subject, ms);
        }
        report.lastLogged = now;
        report.suppressed = 0;
        return SlowOutcome::Logged;
    }

    ResourceMonitor::SlowOutcome ResourceMonitor::ReportOwnTime(const void *key, std::string_view what, std::string_view detail, double chargedBefore, Clock::time_point now) {
        return ReportIfSlow(key, what, detail, ChargedMs(key, now) - chargedBefore, now);
    }

    void ResourceMonitor::SetSlowThresholdMs(double ms) {
        _slowThresholdMs = std::max(0.0, ms);
    }

    std::vector<RuntimeStats> ResourceMonitor::Snapshot() const {
        std::vector<RuntimeStats> out;
        out.reserve(_slots.size());
        for (const auto &[key, slot] : _slots) {
            RuntimeStats stats;
            stats.name      = slot->name;
            double scriptMs = 0.0;
            double gcMs     = 0.0;
            uint64_t ticks  = 0;
            size_t seconds  = 0;
            for (size_t age = 0; age < _filled; ++age) {
                const Second &second = slot->window[(_current + kWindowSeconds - age) % kWindowSeconds];
                scriptMs += second.scriptMs;
                gcMs += second.gcMs;
                ticks += second.ticks;
                stats.scriptMsMaxTick = std::max(stats.scriptMsMaxTick, second.maxTickMs);
                if (second.gcMeasured) {
                    ++seconds;
                }
            }
            stats.scriptMsPerTick = ticks > 0 ? scriptMs / static_cast<double>(ticks) : 0.0;
            stats.gcMsPerSecond   = seconds > 0 ? gcMs / static_cast<double>(seconds) : 0.0;
            stats.scriptSeconds   = slot->totalMs / 1000.0;
            stats.gcPauses        = slot->sample.gcPauses;
            stats.heapUsedBytes   = slot->sample.heapUsedBytes;
            stats.heapTotalBytes  = slot->sample.heapTotalBytes;
            stats.heapLimitBytes  = slot->sample.heapLimitBytes;
            stats.externalBytes   = slot->sample.externalBytes;
            stats.slowReports     = slot->slowReports;
            out.push_back(std::move(stats));
        }
        return out;
    }

} // namespace Framework::Scripting
