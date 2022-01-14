#pragma once

#include <cstdint>

namespace Framework::Utils::States {
    class IState {
      public:
        IState();
        virtual ~IState();

        virtual const char *GetName() const = 0;
        virtual int32_t GetId() const       = 0;

        virtual bool OnEnter() = 0;
        virtual bool OnUpdate() = 0;
        virtual bool OnExit()   = 0;
    };
}
