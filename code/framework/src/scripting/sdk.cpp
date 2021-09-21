#include "sdk.h"

#include "builtins/core.h"
#include "builtins/quaternion.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"
#include "keys.h"
#include "v8_helpers/helpers.h"

namespace Framework::Scripting {
    SDK::SDK(SDKRegisterCallback cb): _rootModule(nullptr) {
        _rootModule = new Helpers::V8Module("sdk", [](v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj) {
            V8Helpers::RegisterFunc(ctx, obj, "on", &Builtins::OnEvent);
        });

        if (!_rootModule) {
            return;
        }

        // Before inserting the reference, we create the required builtins
        Builtins::Vector2Register(_rootModule);
        Builtins::Vector3Register(_rootModule);
        Builtins::QuaternionRegister(_rootModule);

        // Call the SDK register callback for mod-provided implementations
        if (cb) {
            cb(this);
        }

        _modules.insert(_rootModule);
    }

    bool SDK::RegisterModules(v8::Local<v8::Context> context) {
        for (auto mod : _modules) { mod->Register(context); }
        return true;
    }

    v8::Local<v8::Value> SDK::CreateVector2(v8::Local<v8::Context> ctx, glm::vec2 v) {
        auto isolate = v8::Isolate::GetCurrent();
        std::vector<v8::Local<v8::Value>> args {v8::Number::New(isolate, v.x), v8::Number::New(isolate, v.y)};
        Helpers::V8Class *cls = _rootModule->GetClass(GetKeyName(Keys::KEY_VECTOR_2));
        return cls->CreateInstance(isolate, ctx, args);
    }

    v8::Local<v8::Value> SDK::CreateVector3(v8::Local<v8::Context> ctx, glm::vec3 v) {
        auto isolate = v8::Isolate::GetCurrent();
        std::vector<v8::Local<v8::Value>> args {v8::Number::New(isolate, v.x), v8::Number::New(isolate, v.y), v8::Number::New(isolate, v.z)};
        Helpers::V8Class *cls = _rootModule->GetClass(GetKeyName(Keys::KEY_VECTOR_3));
        return cls->CreateInstance(isolate, ctx, args);
    }

    v8::Local<v8::Value> SDK::CreateQuaternion(v8::Local<v8::Context> ctx, glm::quat q) {
        auto isolate = v8::Isolate::GetCurrent();
        std::vector<v8::Local<v8::Value>> args {v8::Number::New(isolate, q.w), v8::Number::New(isolate, q.x), v8::Number::New(isolate, q.y), v8::Number::New(isolate, q.z)};
        Helpers::V8Class *cls = _rootModule->GetClass(GetKeyName(Keys::KEY_QUATERNION));
        return cls->CreateInstance(isolate, ctx, args);
    }
} // namespace Framework::Scripting
