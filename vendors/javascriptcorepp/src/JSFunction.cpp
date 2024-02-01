#include "JavaScriptCorePP/JSFunction.h"
#include "JavaScriptCorePP/JSValue.h"
#include "JavaScriptCorePP/JSObject.h"
#include "JavaScriptCorePP/JSContext.h"

#include <JavaScriptCore/JSContextRef.h>
#include <JavaScriptCore/JSObjectRef.h>
#include <JavaScriptCore/JSValueRef.h>
#include <JavaScriptCore/JSStringRef.h>

#include <JavaScriptCorePP/JSSafeExit.h>

#include <stdexcept>
#include <functional>
#include <vector>
#include <cassert>

namespace JavaScriptCorePP
{
	JSFunction::JSFunction() :
		_value(NULL), _context(NULL) {}

	JSFunction::JSFunction(const JSContext& context, const JSCallback& callback)
		: _context(context)
	{
		static JSClassRef instance = nullptr;
		if (!instance)
		{
			JSClassDefinition def;
			memset(&def, 0, sizeof(def));
			def.className = "NativeFunction";
			def.callAsFunction = JSFunction::StaticCallback;
			def.finalize = JSFunction::StaticFinalize;
			
			instance = JSClassCreate(&def);
		}

		_value = JSObjectMake(_context.GetContextRef(), instance, new JSCallback(callback));
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSFunction::JSFunction(const JSContext& context, JSValueRef value) : _context(context)
	{
		assert(JSValueIsObject(context.GetContextRef(), value));
		this->JSFunction::JSFunction(context, JSValueToObject(context.GetContextRef(), value, NULL));
	}

	JSFunction::JSFunction(const JSContext& context, JSObjectRef value) : _context(context), _value(value)
	{
		assert(JSObjectIsFunction(context.GetContextRef(), value));
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSFunction::JSFunction(const JSFunction& copy) : _value(copy._value), _context(copy._context)
	{
		JSValueProtect(_context.GetContextRef(), _value);
	}
	
	JSFunction::JSFunction(JSFunction&& move) noexcept : _context(move._context), _value(move._value)
	{
		move._context = NULL;
		move._value = NULL;
	}
	
	JSFunction::JSFunction(const JSValue& value) :
		_context(value._context)
	{
		assert(value.IsFunction());
		_value = JSValueToObject(_context.GetContextRef(), value._value, NULL);
		JSValueProtect(_context.GetContextRef(), _value);
	}

	JSFunction::JSFunction(const JSObject& value) :
		_context(value._context)
	{
		assert(JSObjectIsFunction(value._context.GetContextRef(), value._value));
		_value = JSValueToObject(_context.GetContextRef(), value._value, NULL);
		JSValueProtect(_context.GetContextRef(), _value);
	}


	JSFunction::~JSFunction()
	{
		if (_value != NULL && !_js_doSafeExit)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}
	}

	JSFunction::operator JSValue()
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	JSFunction::operator const JSValue() const
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	JSValue JSFunction::operator()() const
	{
		return Call();
	}

	JSValue JSFunction::operator()(const std::vector<JSValue>& args) const
	{
		return Call(args);
	}

	JSValueRef JSFunction::operator*() const
	{
		return _value;
	}

	JSValue JSFunction::Call() const
	{
		JSValueRef error = nullptr;
		auto result = JSObjectCallAsFunction(_context.GetContextRef(), _value, NULL, 0, NULL, &error);

#ifdef JS_ERROR_THROW
		if (error)
		{
			JSValue errorObj(_context, error);

			if (errorObj.IsString())
				throw new std::exception(errorObj.GetString().c_str());

			else
				throw new std::exception(errorObj.ToJSON().c_str());

		}
#endif
		return JSValue::Create(_context, result);
	}

	JSValue JSFunction::Call(const std::vector<JSValue>& args) const
	{
		std::vector<JSValueRef> vals;
		vals.reserve(args.size());

		for (const auto& arg : args)
		{
			vals.push_back(arg._value);
		}

		JSValueRef error = nullptr;
		auto result = JSObjectCallAsFunction(_context.GetContextRef(), _value, _context.GetGlobalObject()._value, args.size(), &vals[0], &error);

#ifdef JS_ERROR_THROW
		if (error)
		{
			JSValue errorObj(_context, error);

			if (errorObj.IsString())
				throw new std::exception(errorObj.GetString().c_str());

			else
				throw new std::exception(errorObj.ToJSON().c_str());
		} 
#endif
		return JSValue::Create(_context, result);
	}

	JSValue JSFunction::ToValue()
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	const JSValue JSFunction::ToValue() const
	{
		return JSValue(_context, (JSValueRef)_value);
	}

	bool JSFunction::Valid() const
	{
		return _value != NULL;
	}

	JSFunction& JSFunction::operator=(const JSFunction& other)
	{
		if (_value != NULL)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}

		_context = other._context;
		_value = other._value;

		if (_value != NULL)
		{
			JSValueProtect(_context.GetContextRef(), _value);
		}

		return *this;
	}

	JSFunction& JSFunction::operator=(JSFunction&& other) noexcept
	{
		_context = other._context;
		_value = other._value;

		other._context = NULL;
		other._value = NULL;

		return *this;
	}

	bool JSFunction::operator==(const JSFunction& rhs) const
	{
		return _value == rhs._value;
	}

	JSValueRef JSFunction::StaticCallback(JSContextRef contextRef, JSObjectRef function, JSObjectRef thisObject, size_t arg_count, const JSValueRef* arguments, JSValueRef* exception)
	{
		const JSCallback* callback = (JSCallback*)JSObjectGetPrivate(function);
		std::vector<JSValue> newArgs;
		newArgs.reserve(arg_count);

		const JSContext context(contextRef);
		for (size_t i = 0; i < arg_count; i++)
		{
			newArgs.emplace_back(context, arguments[i]);
		}

		JSValue returnValue = context.CreateUndefined();
		JSValue returnException = context.CreateUndefined(); 
		
		(*callback)(context, newArgs, returnValue, returnException);

		if(returnException.GetJSType() != JSType::Undefined && returnException.GetJSType() != JSType::Null)
		{
			*exception = returnException._value;
			return NULL;
		}
		
		return returnValue._value;
	}

	void JSFunction::StaticFinalize(JSObjectRef function)
	{
		delete (JSCallback*)JSObjectGetPrivate(function);
	}
}