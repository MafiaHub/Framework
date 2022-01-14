#pragma once

#include "state.h"

#include <map>

namespace Framework::Utils::States {
    enum class Context {
        Enter,
        Update,
        Exit,
        Next // This one does not keep calling, it requests going to the next one
    };

    class Machine {
      private:
        std::map<int32_t, IState *> _states;

        IState *_currentState;
        IState *_nextState;

        Context _currentContext;

      public:
        Machine();
        ~Machine();

        bool RequestNextState(int32_t);
        void RegisterState(IState *);

        bool Update();

        IState *GetCurrentState() const {
            return _currentState;
        }

        IState *GetNextState() const {
            return _nextState;
        }
    };
} // namespace Framework::Utils::States
