/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "state.h"

#include <map>
#include <memory>

namespace Framework::Utils::States {
    enum class Context {
        Enter,
        Update,
        Exit,
        Next // This one does not keep calling, it requests going to the next one
    };

    class Machine {
      private:
        std::map<int32_t, std::shared_ptr<IState>> _states;

        std::shared_ptr<IState> _currentState;
        std::shared_ptr<IState> _nextState;

        Context _currentContext;

      public:
        Machine();
        ~Machine();

        bool RequestNextState(int32_t);

        template<typename T>
        void RegisterState() {
            auto ptr = std::make_shared<T>();
            _states.insert(std::make_pair(ptr->GetId(), ptr));
        }

        bool Update();

        std::shared_ptr<IState> GetCurrentState() const {
            return _currentState;
        }

        std::shared_ptr<IState> GetNextState() const {
            return _nextState;
        }
    };
} // namespace Framework::Utils::States
