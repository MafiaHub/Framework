#pragma once

#include <v8.h>
#include <v8pp/convert.hpp>

namespace Framework::Scripting {
    class ResourceManager;

    class Exports {
      public:
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            ResourceManager *resourceManager);

      private:
        static void RegisterCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static ResourceManager *_resourceManager;
    };

} // namespace Framework::Scripting
