/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "script_log.h"

#include "output_level.h"

#include <logging/logger.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <utility>

namespace Framework::Scripting {
    namespace {
        constexpr std::string_view kTruncated = " ...(truncated)";

        // The second a resource's entries are being counted in, how many it has had in it, and how many were
        // left out since the last one that was written.
        struct Budget {
            std::chrono::steady_clock::time_point second;
            int entries = 0;
            int dropped = 0;
        };

        using Key = std::pair<const void *, std::string>;

        std::mutex &BudgetsMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::map<Key, Budget> &Budgets() {
            static std::map<Key, Budget> budgets;
            return budgets;
        }

        enum class Share {
            Write,
            Warn,
            Drop
        };

        // What the next entry of the resource gets of this second. `droppedBefore` is how many were left out
        // before it, told once, with the first entry that is written again.
        Share Take(const void *owner, std::string_view resourceName, int &droppedBefore) {
            const auto now = std::chrono::steady_clock::now();
            const std::scoped_lock lock(BudgetsMutex());
            auto &budgets = Budgets();
            Key key {owner, std::string(resourceName)};
            auto found = budgets.find(key);
            if (found == budgets.end()) {
                // Resources come and go over the life of a server: forget those that have been quiet.
                std::erase_if(budgets, [now](const auto &entry) {
                    return entry.second.dropped == 0 && now - entry.second.second >= std::chrono::seconds(1);
                });
                found = budgets.emplace(std::move(key), Budget {now, 0, 0}).first;
            }
            Budget &budget = found->second;
            if (now - budget.second >= std::chrono::seconds(1)) {
                budget.second  = now;
                budget.entries = 0;
            }
            if (budget.entries > kMaxScriptErrorsPerSecond) {
                ++budget.dropped;
                return Share::Drop;
            }
            if (budget.entries++ == kMaxScriptErrorsPerSecond) {
                budget.dropped = 1;
                return Share::Warn;
            }
            droppedBefore  = budget.dropped;
            budget.dropped = 0;
            return Share::Write;
        }
    } // namespace

    void BlankControlCharacters(std::string &line) {
        for (size_t i = 0; i < line.size(); ++i) {
            const auto byte = static_cast<unsigned char>(line[i]);
            if ((byte < 0x20 && byte != '\t') || byte == 0x7F) {
                line[i] = ' ';
            }
            else if (byte == 0xC2 && i + 1 < line.size() && static_cast<unsigned char>(line[i + 1]) >= 0x80 && static_cast<unsigned char>(line[i + 1]) <= 0x9F) {
                line[i]     = ' ';
                line[i + 1] = ' ';
            }
        }
    }

    std::string BlankedLine(std::string_view text) {
        std::string line(text);
        BlankControlCharacters(line);
        return line;
    }

    std::string SanitisedScriptText(std::string_view text, std::string_view continuation) {
        const bool whole = text.size() <= kMaxScriptErrorBytes;
        if (!whole) {
            text = text.substr(0, kMaxScriptErrorBytes);
        }

        // The lines of a stack stay lines.
        std::string entry;
        for (size_t start = 0; start <= text.size();) {
            const size_t end = std::min(text.find('\n', start), text.size());
            std::string line(text.substr(start, end - start));
            BlankControlCharacters(line);
            if (start != 0) {
                entry += '\n';
                entry += continuation;
            }
            entry += line;
            start = end + 1;
        }
        if (!whole) {
            entry += kTruncated;
        }
        return entry;
    }

    void LogScriptError(const void *owner, std::string_view resourceName, std::string_view text, OutputLevel level) {
        int droppedBefore = 0;
        const Share share = Take(owner, resourceName, droppedBefore);
        if (share == Share::Drop) {
            return;
        }
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING);
        // An error no resource is known to own stands without a name, its further lines set in from the margin.
        const std::string name = resourceName.empty() ? std::string() : "[" + BlankedLine(resourceName) + "] ";
        if (share == Share::Warn) {
            logger->warn("{}errors suppressed: more than {} in a second", name, kMaxScriptErrorsPerSecond);
            return;
        }
        if (droppedBefore > 0) {
            logger->warn("{}{} error(s) were suppressed", name, droppedBefore);
        }
        logger->log(LogLevelOf(level), "{}{}", name, SanitisedScriptText(text, name.empty() ? std::string_view("    ") : std::string_view(name)));
    }

    void ResetScriptErrorBudget(const void *owner, std::string_view resourceName) {
        const std::scoped_lock lock(BudgetsMutex());
        Budgets().erase(Key {owner, std::string(resourceName)});
    }

    void ForgetScriptErrorBudgets(const void *owner) {
        const std::scoped_lock lock(BudgetsMutex());
        std::erase_if(Budgets(), [owner](const auto &entry) {
            return entry.first.first == owner;
        });
    }
} // namespace Framework::Scripting
