#pragma once

#include <function2.hpp>
#include <string>

#include <v8.h>

namespace Framework::Scripting {
    class Engine;
    using Callback = v8::CopyablePersistentTraits<v8::Function>::CopyablePersistent;
    struct GamemodeMetadata {
        std::string path;
        std::string name;
        std::string version;
        std::string entrypoint;
    };

    class SDKRegisterWrapper final {
      private:
        void *_engine                           = nullptr;

      public:
        SDKRegisterWrapper(void *engine): _engine(engine) {}

        Framework::Scripting::Engine *GetEngine() const {
            return reinterpret_cast<Framework::Scripting::Engine *>(_engine);
        }
    };
    using SDKRegisterCallback = fu2::function<void(SDKRegisterWrapper)>;
}