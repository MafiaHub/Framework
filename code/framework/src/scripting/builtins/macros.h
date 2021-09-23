#pragma once

#define V8_GET_ISOLATE() v8::Isolate *isolate = info.GetIsolate()
#define V8_GET_CONTEXT() v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext()
#define V8_GET_ISOLATE_CONTEXT()                                                                                                                                                   \
    V8_GET_ISOLATE();                                                                                                                                                              \
    V8_GET_CONTEXT()

#define V8_GET_RESOURCE() auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0))

#define V8_GET_SUITE()                                                                                                                                                             \
    V8_GET_ISOLATE();                                                                                                                                                              \
    V8_GET_CONTEXT();                                                                                                                                                              \
    V8_GET_RESOURCE()

#define V8_RETURN(ret) info.GetReturnValue().Set(ret)

#define V8_RETURN_NULL(ret) info.GetReturnValue().SetNull()

#define V8_GET_SELF() v8::Local<v8::Object> _this = info.This()

#define V8_DEFINE_STACK() V8Helpers::ArgumentStack stack(info)

#define V8_VALIDATE_CTOR_CALL()                                                                                                                                                    \
    if (!info.IsConstructCall()) {                                                                                                                                                 \
        V8Helpers::Throw(isolate, "Function cannot be called without new keyword");                                                                                                \
        return;                                                                                                                                                                    \
    }
