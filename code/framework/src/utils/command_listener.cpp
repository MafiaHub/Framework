/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "command_listener.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace Framework::Utils {
    CommandListener::CommandListener() {
        _running = true;
        std::thread([this]() {
            while (_running) {
                std::string commandString;
                if (!std::getline(std::cin, commandString)) {
                    // Non-interactive stdin (e.g. Docker, no TTY) is at EOF and getline
                    // returns instantly; break on EOF, back off otherwise, to avoid a busy-spin.
                    if (std::cin.eof()) {
                        break;
                    }
                    std::cin.clear();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                if (commandString.empty())
                    continue;

                std::scoped_lock lock(_mutex);
                _queue.push(commandString);
            }
        // Thread is detached because std::getline blocks indefinitely and cannot be
        // interrupted portably. The CommandListener must outlive the detached thread
        // or the process must exit shortly after Shutdown().
        }).detach();
    }

    void CommandListener::Update() {
        if (!_cb) {
            return;
        }
        std::scoped_lock lock(_mutex);
        while (!_queue.empty()) {
            const auto &cmdLine = _queue.front();
            _cb(cmdLine);
            _queue.pop();
        }
    }

    void CommandListener::Shutdown() {
        _running = false;
    }
} // namespace Framework::Utils
