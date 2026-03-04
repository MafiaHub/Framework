/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "delay_scope.h"

namespace Framework::Utils {
    DelayScope::DelayScope(uint32_t delay, fu2::function<void()> callback): _callback(std::move(callback)), _delay(delay) {
        _created = std::chrono::high_resolution_clock::now();
    }

    bool DelayScope::FireWhenReady() {
        const auto now          = std::chrono::high_resolution_clock::now();
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - _created);

        if (milliseconds.count() >= _delay) {
            if (_callback != nullptr)
                _callback();

            return true;
        }

        return false;
    }

    void DelayScope::Update() {
        std::vector<std::unique_ptr<DelayScope>> pending;

        for (auto &handler : activeHandlers) {
            if (handler && !handler->FireWhenReady()) {
                pending.push_back(std::move(handler));
            }
        }

        activeHandlers = std::move(pending);
    }

    void DelayScope::Push(uint32_t delay, fu2::function<void()> callback) {
        activeHandlers.push_back(std::make_unique<DelayScope>(delay, std::move(callback)));
    }

    std::vector<std::unique_ptr<DelayScope>> DelayScope::activeHandlers;
} // namespace Framework::Utils
