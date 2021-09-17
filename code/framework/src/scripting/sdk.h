#pragma once

#include "init.h"
#include "v8_helpers/v8_class.h"
#include "v8_helpers/v8_module.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_set>
#include <v8.h>
#include <vector>

namespace Framework::Scripting {
    class SDK {
      private:
        std::unordered_set<Helpers::V8Module *> _modules;
        Helpers::V8Module *_rootModule;

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

        v8::Local<v8::Value> CreateVector2(v8::Local<v8::Context>, glm::vec2);
        v8::Local<v8::Value> CreateVector3(v8::Local<v8::Context>, glm::vec3);
        v8::Local<v8::Value> CreateQuaternion(v8::Local<v8::Context>, glm::quat);
    };
} // namespace Framework::Scripting
