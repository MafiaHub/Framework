/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "script_log.h"

#include <logging/logger.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>

namespace Framework::Scripting {
    namespace {
        // The second a resource's errors are being counted in, and how many it has had in it.
        struct Budget {
            std::chrono::steady_clock::time_point second;
            int entries = 0;
        };

        enum class Share {
            Write,
            Warn,
            Drop
        };

        // What the next error of the resource gets of this second.
        Share Take(std::string_view resourceName) {
            static std::mutex mutex;
            static std::map<std::string, Budget, std::less<>> budgets;

            const auto now = std::chrono::steady_clock::now();
            const std::scoped_lock lock(mutex);
            auto found = budgets.find(resourceName);
            if (found == budgets.end()) {
                // Resources come and go over the life of a server: forget those that have been quiet.
                std::erase_if(budgets, [now](const auto &entry) {
                    return now - entry.second.second >= std::chrono::seconds(1);
                });
                found = budgets.emplace(std::string(resourceName), Budget {now, 0}).first;
            }
            Budget &budget = found->second;
            if (now - budget.second >= std::chrono::seconds(1)) {
                budget = {now, 0};
            }
            if (budget.entries > kMaxScriptErrorsPerSecond) {
                return Share::Drop;
            }
            return budget.entries++ < kMaxScriptErrorsPerSecond ? Share::Write : Share::Warn;
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

    void LogScriptError(std::string_view resourceName, std::string_view text) {
        const Share share = Take(resourceName);
        if (share == Share::Drop) {
            return;
        }
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING);
        if (share == Share::Warn) {
            logger->warn("[{}] errors suppressed: more than {} in a second", resourceName, kMaxScriptErrorsPerSecond);
            return;
        }

        // The lines of a traceback stay lines, each under the name of the resource.
        std::string entry;
        for (size_t start = 0; start <= text.size();) {
            const size_t end = std::min(text.find('\n', start), text.size());
            std::string line(text.substr(start, end - start));
            BlankControlCharacters(line);
            entry += start == 0 ? "[" : "\n[";
            entry += resourceName;
            entry += "] ";
            entry += line;
            start = end + 1;
        }
        logger->error("{}", entry);
    }
} // namespace Framework::Scripting
