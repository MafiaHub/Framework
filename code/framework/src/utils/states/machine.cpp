/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "machine.h"
#include <logging/logger.h>

namespace Framework::Utils::States {
    Machine::Machine(): _currentState(nullptr), _nextState(nullptr), _currentContext(Context::Enter), _isUpdating(false) {}

    Machine::~Machine() {
        std::lock_guard<std::mutex> lock(_mutex);
        _states.clear();
    }

    bool Machine::RequestNextState(int32_t stateId) {
        std::lock_guard<std::mutex> lock(_mutex);   

        auto it = _states.find(stateId);
        if (it == _states.end()) {
            return false;
        }

        if (it->second.get() == _nextState) {
            return false;
        }

        if (_nextState != nullptr) {
            return false;
        }

        _nextState = it->second.get();
        _currentContext = Context::Exit;

        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->debug("[StateMachine] Requesting new state {}", _nextState->GetName());
        return true;
    }

   bool Machine::Update() {
        std::unique_lock<std::mutex> lock(_mutex);

        if (_isUpdating) {
            return false;
        }

        _isUpdating = true;

        IState *currentState   = _currentState;
        IState *nextState      = _nextState;
        Context currentContext = _currentContext;

        if (!currentState && !nextState) {
            _isUpdating = false;
            return false;
        }

        if (!currentState && nextState) {
            _currentState   = nextState;
            _currentContext = Context::Enter;
            _nextState      = nullptr;
            _isUpdating     = false;
            return true;
        }

        lock.unlock();

        bool result = false;
        try {
            switch (currentContext) {
            case Context::Enter: {
                result = currentState->OnEnter(this);
                lock.lock();
                _currentContext = result ? Context::Update : Context::Exit;
                break;
            }
            case Context::Update: {
                result = currentState->OnUpdate(this);
                lock.lock();
                if (result)
                    _currentContext = Context::Exit;
                break;
            }
            case Context::Exit: {
                result = currentState->OnExit(this);
                lock.lock();
                _currentContext = Context::Next;
                break;
            }
            case Context::Next: {
                lock.lock();
                _currentState   = nextState;
                _currentContext = Context::Enter;
                _nextState      = nullptr;
                break;
            }
            default:
                lock.lock();
                _isUpdating = false;
                return false;
            }
        }
        catch (const std::exception &e) {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->error("[StateMachine] Error in state {}: {}", currentState ? currentState->GetName() : "null", e.what());
            lock.lock();
            _isUpdating = false;
            throw;
        }

        _isUpdating = false;
        return true;
    }
} // namespace Framework::Utils::States
