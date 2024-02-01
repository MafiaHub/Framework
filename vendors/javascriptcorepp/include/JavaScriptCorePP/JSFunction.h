#pragma once

#include <vector>
#include "JSContext.h"

namespace JavaScriptCorePP
{
	class JSObject;
	class JSValue;

	typedef std::function<void(const JSContext&, const std::vector<JSValue>&, JSValue&, JSValue&)> JSCallback;

	class JSFunction
	{
	public:
		JSFunction();
		JSFunction(const JSContext& context, const JSCallback& callback);
		JSFunction(const JSContext& context, JSValueRef value);
		JSFunction(const JSContext& context, JSObjectRef value);
		JSFunction(const JSFunction& copy);
		JSFunction(JSFunction&& move) noexcept;
		JSFunction(const JSValue& value);
		JSFunction(const JSObject& value);
		~JSFunction();

		operator JSValue();
		operator const JSValue() const;
		JSValue operator()() const;
		JSValue operator()(const std::vector<JSValue>& args) const;
		JSValueRef operator*() const;

		JSValue Call() const;
		JSValue Call(const std::vector<JSValue>& args) const;

		JSValue ToValue();
		const JSValue ToValue() const;

		bool Valid() const;

		JSFunction& operator=(const JSFunction& other);
		JSFunction& operator=(JSFunction&& other) noexcept;

		bool operator==(const JSFunction& rhs) const;

	protected:
		JSObjectRef _value = NULL;
		JSContext _context = NULL;

		static JSValueRef StaticCallback(JSContextRef contextRef,
			JSObjectRef f,
			JSObjectRef thisObject,
			size_t arg_count,
			const JSValueRef* arguments,
			JSValueRef* exception);

		static void StaticFinalize(JSObjectRef function);
	};
}