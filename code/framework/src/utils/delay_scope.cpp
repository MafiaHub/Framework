/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "delay_scope.h"

namespace Framework::Utils {
    DelayScope::DelayScope(uint32_t delay, std::function<void()> callback): _delay(delay), _callback(callback) {
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
        std::vector<DelayScope *> pendingDelays;

        for (auto handler : activeHandlers) {
            if (handler != nullptr && !handler->FireWhenReady()) {
                pendingDelays.push_back(handler);
            }
            else {
                delete handler;
            }
        }

        activeHandlers = pendingDelays;
    }

    void DelayScope::Push(uint32_t delay, std::function<void()> callback) {
        activeHandlers.push_back(new DelayScope(delay, callback));
    }

    std::vector<DelayScope *> DelayScope::activeHandlers;
} // namespace Framework::Utils
