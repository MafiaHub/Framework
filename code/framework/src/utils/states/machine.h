/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "state.h"

#include <map>
#include <memory>
#include <mutex>

namespace Framework::Utils::States {
    class StateTransitionError : public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    enum class Context {
        Enter,
        Update,
        Exit,
        Next
    };

    class Machine {
      private:
        mutable std::mutex _mutex;
        std::map<int32_t, std::unique_ptr<IState>> _states;
        IState* _currentState;
        IState* _nextState;
        Context _currentContext;
        bool _isUpdating;

      public:
        Machine();
        ~Machine();

        // Prevent copying
        Machine(const Machine&) = delete;
        Machine& operator=(const Machine&) = delete;

        bool RequestNextState(int32_t);

        template <typename T>
        void RegisterState() {
            std::lock_guard<std::mutex> lock(_mutex);
            auto ptr = std::make_unique<T>();
            int32_t id = ptr->GetId();
            
            if (_states.find(id) != _states.end()) {
                throw std::runtime_error("State ID already registered");
            }
            
            _states.emplace(id, std::move(ptr));
        }

        bool Update();

        const IState* GetCurrentState() const {
            std::lock_guard<std::mutex> lock(_mutex);
            return _currentState;
        }

        const IState* GetNextState() const {
            std::lock_guard<std::mutex> lock(_mutex);
            return _nextState;
        }
    };
} // namespace Framework::Utils::States
