/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "command_listener.h"

#include <iostream>
#include <mutex>
#include <thread>

namespace Framework::Utils {
    CommandListener::CommandListener() {
        _currentThread = std::make_shared<std::thread>([this]() {
            while (true) {
                std::string commandString;
                std::getline(std::cin, commandString);

                if (commandString.empty())
                    continue;

                std::lock_guard<std::mutex> lock(_mutex);
                _queue.push(commandString);
            }
        });

        _currentThread->detach();
    }

    void CommandListener::Update() {
        if (!_cb) {
            return;
        }
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_queue.empty()) {
            const auto &cmdLine = _queue.front();
            _cb(cmdLine);
            _queue.pop();
        }
    }
} // namespace Framework::Utils
