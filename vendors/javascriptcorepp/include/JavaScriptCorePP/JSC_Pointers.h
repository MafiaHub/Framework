#pragma once

struct OpaqueJSContext;
typedef const OpaqueJSContext* JSContextRef;
typedef struct OpaqueJSContext* JSGlobalContextRef;

struct OpaqueJSValue;
typedef const struct OpaqueJSValue* JSValueRef;
typedef struct OpaqueJSValue* JSObjectRef;

typedef struct OpaqueJSString* JSStringRef;

typedef struct OpaqueJSClass* JSClassRef;

typedef JSValueRef (*JSObjectCallAsFunctionCallback) (JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception);