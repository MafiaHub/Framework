#include "machine.h"

namespace Framework::Utils::States {
    Machine::Machine() {

    }

    Machine::~Machine() {
        _states.clear();
    }

    void Machine::RegisterState(IState* state) {
        _states.insert({state->GetId(), state});
    }

    bool Machine::RequestNextState(int32_t stateId) {
        auto it = _states.find(stateId);
        if (it == _states.end()) {
            return false;
        }

        // The state has already been requested
        if ((*it).second->GetId() == _nextState->GetId()) {
            return false;
        }

        // Mark it for processing and force the actual state to exit
        _nextState = (*it).second;
        _currentContext = Context::Exit;
        return true;
    }

    bool Machine::Update() {
        // If we don't have a current state, the next state becomes the state
        if (!_currentState) {
            _currentState   = _nextState;
            _currentContext = Context::Enter;
            _nextState      = nullptr;
            return true;
        }

        // Otherwise, we just process the current state
        if (_currentContext == Context::Enter) {
            // If init succeed, next context is obviously the update, otherwise it means that something failed and exit is required
            _currentContext = _currentState->OnEnter() ? Context::Update : Context::Exit;
        }
        else if (_currentContext == Context::Update) {
            // If the state answer true to update call, it means that it willed only a single tick, otherwise we keep ticking
            if (_currentState->OnUpdate()) {
                _currentContext = Context::Exit;
            }
        }
        else if (_currentContext == Context::Exit) {
            _currentState->OnExit();
            _currentContext = Context::Next;
        }
        else if (_currentContext == Context::Next) {
            _currentState = _nextState;
            _currentContext = Context::Enter;
            _nextState      = nullptr;
        }
        else {
            return false;
        }
        return true;
    }
}
