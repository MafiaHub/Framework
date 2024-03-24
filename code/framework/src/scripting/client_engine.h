#pragma once

#include "errors.h"
#include "shared.h"

#include "engine.h"

namespace Framework::Scripting {
    class ClientEngine : public Engine {
        public:
            ClientEngine() = default;
            ~ClientEngine() = default;

            EngineError Init(SDKRegisterCallback) override;
            EngineError Shutdown() override;
            void Update() override;
    };
}