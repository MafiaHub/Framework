/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "tick_breakdown.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace Framework::Integrations::Server {
    std::string TickBreakdown::Describe() const {
        constexpr double kShownMs = 1.0;
        struct Phase {
            const char *name;
            double ms;
        };
        std::array<Phase, 5> phases {{
            {"scripts", scriptsMs},
            {"replication", replicationInterestMs + replicationPassMs},
            {"packets", packetsMs},
            {"game", gameMs},
            {"other", otherMs},
        }};
        std::stable_sort(phases.begin(), phases.end(), [](const Phase &a, const Phase &b) {
            return a.ms > b.ms;
        });

        std::string out;
        for (const Phase &phase : phases) {
            if (phase.ms < kShownMs) {
                continue;
            }
            if (!out.empty()) {
                out += ", ";
            }
            out += fmt::format("{} {:.0f} ms", phase.name, std::round(phase.ms));
            if (std::string_view(phase.name) == "replication") {
                out += fmt::format(" (interest {:.0f}, pass {:.0f})", std::round(replicationInterestMs), std::round(replicationPassMs));
            }
        }
        return out;
    }
} // namespace Framework::Integrations::Server
