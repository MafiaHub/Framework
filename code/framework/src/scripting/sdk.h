#pragma once

#include "init.h"
#include "v8_helpers/v8_module.h"

#include <unordered_set>
#include <v8.h>

namespace Framework::Scripting {
    class SDK {
      private:
        std::unordered_set<Helpers::V8Module *> _modules;
        Helpers::V8Module *_rootModule;

        void RegisterBuiltins();

      public:
        SDK(SDKRegisterCallback = nullptr);
        ~SDK() = default;

        bool RegisterModules(v8::Local<v8::Context>);

        Helpers::V8Module *GetRootModule() const {
            return _rootModule;
        }

        std::unordered_set<Helpers::V8Module *> &GetModules() {
            return _modules;
        }
    };
} // namespace Framework::Scripting
