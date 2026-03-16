#include "environment.h"

#include <v8pp/convert.hpp>

namespace Framework::Scripting {

    void Environment::Register(v8::Isolate *isolate,
                              v8::Local<v8::Context> context,
                              v8::Local<v8::Object> target,
                              bool isClient) {
        v8pp::module env(isolate);
        env.const_("IsClient", isClient);
        env.const_("IsServer", !isClient);

        target->Set(context, v8pp::to_v8(isolate, "Environment"), env.new_instance()).Check();
    }

} // namespace Framework::Scripting
