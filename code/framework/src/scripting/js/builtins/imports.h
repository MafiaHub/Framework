#pragma once

#include "../v8_helpers.h"

#include <v8.h>

#include <string>

namespace Framework::Scripting::JS {

    class JSResourceManager;

    /**
     * Resource imports system for accessing exports from other resources.
     * Provides Framework.imports API:
     * - get(resourceName) - Returns exports from another resource
     */
    class Imports {
      public:
        /**
         * Register the Framework.imports object in a context.
         * @param isolate V8 isolate
         * @param context Target context
         * @param frameworkObj Framework global object to attach to
         * @param resourceManager Resource manager for getting exports
         */
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            JSResourceManager *resourceManager);

      private:
        // V8 callback implementations
        static void GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static JSResourceManager *_resourceManager;
    };

} // namespace Framework::Scripting::JS
