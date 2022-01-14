/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "machine.h"

#include <logging/logger.h>

namespace Framework::Utils::States {
    Machine::Machine(): _currentState(nullptr), _nextState(nullptr), _currentContext(Context::Enter) {

    }

    Machine::~Machine() {
        _states.clear();
    }

    bool Machine::RequestNextState(int32_t stateId) {
        // Has the state been registered?
        auto it = _states.find(stateId);
        if (it == _states.end()) {
            return false;
        }

        // The state has already been requested
        if ((*it).second == _nextState) {
            return false;
        }

        if (_nextState != nullptr) {
            return false;
        }

        // Mark it for processing and force the actual state to exit
        _nextState = (*it).second;
        _currentContext = Context::Exit;
        
        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->debug("[StateMachine] Requesting new state {}", _nextState->GetName());
        return true;
    }

    bool Machine::Update() {        
        if (_currentState != nullptr) {
            // Otherwise, we just process the current state
            if (_currentContext == Context::Enter) {
                // If init succeed, next context is obviously the update, otherwise it means that something failed and exit is required
                _currentContext = _currentState->OnEnter(this) ? Context::Update : Context::Exit;
            }
            else if (_currentContext == Context::Update) {
                // If the state answer true to update call, it means that it willed only a single tick, otherwise we keep ticking
                if (_currentState->OnUpdate(this)) {
                    _currentContext = Context::Exit;
                }
            }
            else if (_currentContext == Context::Exit) {
                _currentState->OnExit(this);
                _currentContext = Context::Next;
            }
            else if (_currentContext == Context::Next) {
                _currentState   = _nextState;
                _currentContext = Context::Enter;
                _nextState      = nullptr;
            }
            else {
                return false;
            }
        }
        else if (_nextState != nullptr) {
            _currentState   = _nextState;
            _currentContext = Context::Enter;
            _nextState      = nullptr;
        }
        else {
            return false;
        }

        return true;
    }
}
