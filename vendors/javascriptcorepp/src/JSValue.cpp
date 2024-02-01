#include "JavaScriptCorePP/JSValue.h"
#include "JavaScriptCorePP/JSFunction.h"
#include "JavaScriptCorePP/JSObject.h"
#include "JavaScriptCorePP/JSContext.h"
#include "JavaScriptCorePP/JSString.h"

#include <JavaScriptCorePP/JSSafeExit.h>

#include <JavaScriptCore/JSContextRef.h>
#include <cassert>

namespace JavaScriptCorePP
{
	JSValue::JSValue() : _value(NULL), _type(JSType::Invalid), _context(NULL) {}

	JSValue::JSValue(const JSContext& context, JSValueRef value) :
		_context(context), _value(value)
	{ 
		_type = (JSType)JSValueGetType(_context.GetContextRef(), _value);

		if (IsObject() && JSObjectIsFunction(_context.GetContextRef(), JSValueToObject(_context.GetContextRef(), _value, NULL)))
		{
			_type = JSType::Function;
		}
		
		JSValueProtect(context.GetContextRef(), _value);
	}

	JSValue::JSValue(const JSValue& copy) :
		_context(copy._context), _value(copy._value), _type(copy._type)
	{
		JSValueProtect(_context.GetContextRef(), _value);
	}
	
	JSValue::JSValue(JSValue&& move) noexcept :
		_value(move._value), _context(move._context), _type(move._type)
	{
		move._value = NULL;
		move._context = NULL;
		move._type = JSType::Invalid;
	}
	
	JSValue::~JSValue()
	{
		if(_value != NULL && !_js_doSafeExit)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}
	}

	JSFunction JSValue::GetFunction()
	{
		assert(IsFunction());
		return JSFunction(*this);
	}

	const JSFunction JSValue::GetFunction() const
	{
		assert(IsFunction());
		return JSFunction(*this);
	}

	JSValue JSValue::Create(JSContext context, JSValueRef value)
	{
		return JSValue(context, value);
	}

	IndexedJSValue<std::string> JSValue::operator[](const std::string& key)
	{
		assert(IsObject());
		return GetJSObject()[key];
	}

	IndexedJSValue<unsigned int> JSValue::operator[](unsigned int key)
	{
		assert(IsObject());
		return GetJSObject()[key];
	}

	JSValue& JSValue::operator=(const JSValue& other)
	{
		if (_value != NULL)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}

		_context = other._context;
		_value = other._value;
		_type = other._type;

		if (_value != NULL)
		{
			JSValueProtect(_context.GetContextRef(), _value);
		}

		return *this;
	}

	JSValue& JSValue::operator=(JSValue&& other)
	{
		if (_value != NULL)
		{
			JSValueUnprotect(_context.GetContextRef(), _value);
		}

		_context = other._context;
		_value = other._value;
		_type = other._type;

		other._value = NULL;
		other._context = NULL;
		other._type = JSType::Invalid;

		return *this;
	}

	bool JSValue::operator==(const JSValue& other)
	{
		return _type == other._type && _context == other._context && JSValueIsEqual(_context.GetContextRef(), _value, other._value, NULL);
	}

	JSValueRef JSValue::operator*() const
	{
		return _value;
	}

	JSObject JSValue::GetJSObject()
	{
		assert(IsObject());
		return JSObject(*this);
	}

	const JSObject JSValue::GetJSObject() const
	{
		assert(IsObject());
		return JSObject(*this);
	}

	JSType JSValue::GetJSType() const
	{
		return _type;
	}

	bool JSValue::IsUndefined() const
	{
		return _type == JSType::Undefined;
	}

	bool JSValue::IsNull() const
	{
		return _type == JSType::Null;
	}

	bool JSValue::IsBoolean() const
	{
		return _type == JSType::Boolean;
	}

	bool JSValue::IsNumber() const
	{
		return _type == JSType::Number;
	}

	bool JSValue::IsString() const
	{
		return _type == JSType::String;
	}

	bool JSValue::IsObject() const
	{
		return _type == JSType::Object;
	}

	bool JSValue::IsFunction() const
	{
		return _type == JSType::Function;
	}

	std::string JSValue::ToJSON() const
	{
		return JSString(_context, JSValueCreateJSONString(_context.GetContextRef(), _value, 0, NULL)).GetString();
	}

	bool JSValue::Valid() const
	{
		return _type != JSType::Invalid;
	}

	bool JSValue::GetBoolean() const
	{
		assert(IsBoolean());
		return JSValueToBoolean(_context.GetContextRef(), _value);
	}

	double JSValue::GetNumber() const
	{
		assert(IsNumber());
		return JSValueToNumber(_context.GetContextRef(), _value, NULL);
	}

	std::string JSValue::GetString() const
	{
		assert(IsString());
		return JSString(_context, _value).GetString();
	}

	std::wstring JSValue::GetWString() const
	{
		assert(IsString());
		return JSString(_context, _value).GetWString();
	}
}