#include "command_processor.h"

#include <iostream>
#include <mutex>

namespace Framework::Utils {
    CommandProcessor::CommandProcessor() {
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

    void CommandProcessor::Update() {
        std::lock_guard<std::mutex> lock(_mutex);
        while (!_queue.empty()) {
            const auto &cmdLine = _queue.front();
            if (_cb) {
                _cb(cmdLine);
            }
            _queue.pop();
        }
    }
} // namespace Framework::Utils